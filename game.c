/*
** game.c
**
** Author: Peter Sutton
**
*/

#include "game.h"
#include "ledmatrix.h"
#include "pixel_colour.h"
#include "score.h"
#include <stdlib.h>
/* Stdlib needed for random() - random number generator */
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "buttons.h"
#include "terminalio.h"
#include "display.h"
#include "sound.h"

///////////////////////////////////////////////////////////
// Colours
#define COLOUR_ASTEROID		COLOUR_GREEN
#define COLOUR_PROJECTILE	COLOUR_RED
#define COLOUR_BASE			COLOUR_YELLOW

#undef _ASTEROID_DEBUG

///////////////////////////////////////////////////////////
// Game positions (x,y) where x is 0 to 7 and y is 0 to 15
// are represented in a single 8 bit unsigned integer where the most
// significant 4 bits are the x value and the least significant 4 bits
// are the y value. The following macros allow the extraction of x and y
// values from a combined position value and the construction of a combined 
// position value from separate x, y values. Values are assumed to be in
// valid ranges. Invalid positions are any where the least significant
// bit is 1 (i.e. x value greater than 7). We can use all 1's (255) to 
// represent this.
#define GAME_POSITION(x,y)		( ((x) << 4)|((y) & 0x0F) )
#define GET_X_POSITION(posn)	((posn) >> 4)
#define GET_Y_POSITION(posn)	((posn) & 0x0F)
#define INVALID_POSITION		255

#define TERM_POS_FROM_GAME_POS(pos) (GET_X_POSITION(pos)*2+X_LEFT+1), (Y_BOTTOM-1-GET_Y_POSITION(pos))

///////////////////////////////////////////////////////////
// Macros to convert game position to LED matrix position
// Note that the row number (y value) in the game (0 to 15 from the bottom) 
// corresponds to x values on the LED matrix (0 to 15).
// Column numbers (x values) in the game (0 to 7 from the left) correspond
// to LED matrix y values from 7 to 0
//
// Note that these macros result in two expressions that are comma separated - suitable
// as use for the first two arguments to ledmatrix_update_pixel().
#define LED_MATRIX_POSN_FROM_XY(gameX, gameY)		(gameY) , (7-(gameX))
#define LED_MATRIX_POSN_FROM_GAME_POSN(posn)		\
		LED_MATRIX_POSN_FROM_XY(GET_X_POSITION(posn), GET_Y_POSITION(posn))

///////////////////////////////////////////////////////////
// Global variables.
//
// basePosition - stores the x position of the centre point of the 
// base station. The base station is three positions wide, but is
// permitted to partially move off the game field so that the centre
// point can take on any position from 0 to 7 inclusive.
//
// numProjectiles - The number of projectiles currently in flight. Must
// be less than or equal to MAX_PROJECTILES.
//
// projectiles - x,y positions of the projectiles that are currently
// in flight. The upper 4 bits represent the x position; the lower 4
// bits represent the y position. The array is indexed by projectile
// number from 0 to numProjectiles - 1.
//
// numAsteroids - The number of asteroids currently on the game field.
// Must be less than or equal to MAX_ASTEROIDS.
//
// asteroids - x,y positions of the asteroids on the field. The upper
// 4 bits represent the x position; the lower 4 bits represent the 
// y position. The array is indexed by asteroid number from 0 to 
// numAsteroids - 1.

int8_t		basePosition;
int8_t		numProjectiles;
uint8_t		projectiles[MAX_PROJECTILES];
int8_t		numAsteroids;
uint8_t		asteroids[MAX_ASTEROIDS];

///////////////////////////////////////////////////////////
// Prototypes for internal information functions 
//  - not available outside this module.

// Is there is an asteroid/projectile at the given position?. 
// Returns -1 if no, asteroid/projectile index number if yes.
// (The index number is the array index in the asteroids/
// projectiles array above.)

static int8_t asteroid_at(uint8_t x, uint8_t y);
static int8_t projectile_at(uint8_t x, uint8_t y);
static void check_all_base_hits();

static void add_asteroid_in_rows(uint8_t min_y);

static int8_t check_asteroid_hit(int8_t projectileIndex, int8_t asteroidHit);
static void add_missing_asteroids(void);

// Remove the asteroid/projectile at the given index number. If
// the index is not valid, then no removal is performed. This 
// enables the functions to be used like:
//		remove_asteroid(asteroid_at(x,y));
static void remove_asteroid(int8_t asteroidIndex);
static void remove_projectile(int8_t projectileIndex);

static void add_asteroid(void);

// Redraw functions
static void redraw_whole_display(void);
static void redraw_base(uint8_t colour);
static void redraw_all_asteroids(void);
static void redraw_asteroid(uint8_t asteroidNumber, uint8_t colour);
static void redraw_all_projectiles(void);
static void redraw_projectile(uint8_t projectileNumber, uint8_t colour);
///////////////////////////////////////////////////////////

/*typedef enum modes { MODE_NONE, MODE_ASTEROID, MODE_PROJECTILE, MODE_BASE } TerminalMode;*/
/*static TerminalMode currentMode = MODE_NONE;*/

uint8_t paused = 0;

void _debug_asteroids() {
	move_cursor(2, 10);
	printf_P(PSTR("DEBUG ASTEROIDS\n"));
	for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
		printf("%d [%d] = (%d, %d)\n", i<numAsteroids, i, GET_X_POSITION(asteroids[i]), GET_Y_POSITION(asteroids[i]));
		
	}
}

void sort_asteroids() {
	uint8_t i, j;
	uint8_t temp;
	// from https://www.geeksforgeeks.org/insertion-sort/
	for (i = 1; i < numAsteroids; i++) {
		/* invariant:  array[0..i-1] is sorted */
		j = i;
		/* customization bug: SWAP is not used here */
		temp = asteroids[j];
		while (j > 0 && (GET_Y_POSITION(asteroids[j-1]) > GET_Y_POSITION(temp))) {
			asteroids[j] = asteroids[j-1];
			j--;
		}
		asteroids[j] = temp;
	}
}
 
// Initialise game field:
// (1) base starts in the centre (x=3)
// (2) no projectiles initially
// (3) the maximum number of asteroids, randomly distributed.
void initialise_game(void) {
	reset_frame();
    basePosition = 3;
	numProjectiles = 0;
	numAsteroids = 0;
	paused = 0;
	new_frame();
	for(uint8_t i=0; i < MAX_ASTEROIDS ; i++) {
		add_asteroid();
	}
	
	sort_asteroids();
	/*_debug_asteroids();*/
	
	
	redraw_whole_display();
	draw_frame();
}

void add_asteroid(void) {
	add_asteroid_in_rows(3);
}

/*  rows is how many rows the asteroid can be in,
	starting at the top of the board. */
void add_asteroid_in_rows(uint8_t blockedRows) {
	if (numAsteroids == MAX_ASTEROIDS)
		return;
	
	uint8_t x, y;
	uint8_t i = numAsteroids;
	
	uint8_t attempts = 0;
	// Generate random position that does not already
	// have an asteroid.
	do {
		// Generate random x position - somewhere from 0
		// to FIELD_WIDTH - 1
		x = (uint8_t)(random() % FIELD_WIDTH);
		// Generate random y position - somewhere from 3
		// to FIELD_HEIGHT - 1 (i.e., not in the lowest
		// three rows)
		y = (uint8_t)(blockedRows + (random() % (FIELD_HEIGHT-blockedRows)));
		if (asteroid_at(x, y) == -1) {
			// If we get here, we've now found an x,y location without
			// an existing asteroid - record the position
			asteroids[i] = GAME_POSITION(x,y);
			numAsteroids++;
			redraw_asteroid(i, COLOUR_ASTEROID);
			return;
		}
		attempts++;
	} while (attempts <= 8*16);
	
	#ifdef _ASTEROID_DEBUG
	_debug_asteroids();
	printf("add_asteroids_in_rows");
	#endif

}

// Attempt to move the base station to the left or right. 
// The direction argument has the value MOVE_LEFT or
// MOVE_RIGHT. The move succeeds if the base isn't all 
// the way to one side, e.g., not permitted to move
// left if basePosition is already 0.
// Returns 1 if move successful, 0 otherwise.
int8_t move_base(int8_t direction) {	
	// The initial version of this function just moves
	// the base one position to the left, no matter where
	// the base station is now or what the direction argument
	// is. This may cause the base to move off the game field
	// (and eventually wrap around - e.g. subtracting 1 from
	// basePosition 256 times will eventually bring it back to
	// same value.
	
	// #define MOVE_LEFT 0
	// #define MOVE_RIGHT 1
	
	// YOUR CODE HERE (AND BELOW) - FIX THIS FUNCTION
	
	if ((basePosition == 0 && direction == MOVE_LEFT) 
		|| (basePosition == 7 && direction == MOVE_RIGHT))
		return 0;
	draw_frame();
	s_invalidate_mode();
	// We erase the base from its current position first
	redraw_base(COLOUR_BLACK);
	
	// Move the base (only to the left at present)
	basePosition += 2*direction - 1;
	check_all_base_hits();
	
	// Redraw the base
	redraw_base(COLOUR_BASE);
	draw_frame();
	
	return 1;
}

// Fire projectile - add it immediately above the base
// station, provided there is not already a projectile
// there. We are also limited in the number of projectiles
// we can have in flight (to MAX_PROJECTILES).
// Returns 1 if projectile fired, 0 otherwise.
int8_t fire_projectile(void) {
	uint8_t newProjectileNumber;
	
	if(numProjectiles < MAX_PROJECTILES && 
			projectile_at(basePosition, 2) == -1) {
		// Have space to add projectile - add it at the x position of
		// the base, in row 2(y=2)
		new_frame();
		newProjectileNumber = numProjectiles++;
		projectiles[newProjectileNumber] = GAME_POSITION(basePosition, 2);
		if (check_asteroid_hit(newProjectileNumber, asteroid_at(basePosition, 2)) != -1) {
			redraw_projectile(newProjectileNumber, COLOUR_PROJECTILE);
		}
		draw_frame();
		return 1;
	} else {
		return 0;
	}
}

// Move projectiles up by one position, and remove those that 
// have gone off the top or that hit an asteroid.
void advance_projectiles(void) {
	uint8_t x, y;
	int8_t projectileNumber;
	new_frame();
	projectileNumber = 0;
	s_invalidate_mode();
	while(projectileNumber < numProjectiles) {
		// Get the current position of the projectile
		x = GET_X_POSITION(projectiles[projectileNumber]);
		y = GET_Y_POSITION(projectiles[projectileNumber]);
		
		// Work out the new position (but don't update the projectile 
		// location yet - we only do that if we know the move is valid)
		y = y+1;
		
		// Check if new position would be off the top of the display
		if(y == FIELD_HEIGHT) {
			// Yes - remove the projectile. (Note that we haven't updated
			// the position of the projectile itself - so the projectile 
			// will be removed from its old location.)
			remove_projectile(projectileNumber);
			// Note - we do not increment the projectileNumber here as
			// the remove_projectile() function moves the later projectiles
			// (if any) back down the list of projectiles so that
			// the projectileNumber is now the next projectile to be
			// dealt with (if we weren't at the last one in the list).
			// remove_projectile() will also result in numProjectiles being
			// decreased by 1
			continue;
		}
		
		if (check_asteroid_hit(projectileNumber, asteroid_at(x, y)))
			continue;
			
		// Projectile is not going off the top of the display
		// CHECK HERE IF THE NEW PROJECTILE LOCATION CORRESPONDS TO
		// AN ASTEROID LOCATION. IF IT DOES, REMOVE THE PROJECTILE
		// AND THE ASTEROID.
			
		// OTHERWISE...
			
		// Remove the projectile from the display 
		redraw_projectile(projectileNumber, COLOUR_BLACK);
			
		// Update the projectile's position
		projectiles[projectileNumber] = GAME_POSITION(x,y);
			
		// Redraw the projectile
		redraw_projectile(projectileNumber, COLOUR_PROJECTILE);
			
		// Move on to the next projectile (we don't do this if a projectile
		// is removed since projectiles will be shuffled in the list and the
		// next projectile (if any) will take on the same projectile number)
		projectileNumber++;
	}
	add_missing_asteroids();
	draw_frame();
}

int8_t check_asteroid_hit(int8_t projectileIndex, int8_t asteroidHit) {
	if (projectileIndex == -1 || asteroidHit == -1)
		return 0;
	remove_projectile(projectileIndex);
	remove_asteroid(asteroidHit);
	add_to_score(1);
	play_track(TRACK_COIN);
	return 1;
}

static uint8_t check_base_hit(int8_t x, int8_t y) {
	int8_t asteroid = asteroid_at(x, y);
	if (asteroid == -1)
		return 0;
	
	remove_asteroid(asteroid);
	
	/*ledmatrix_update_pixel(LED_MATRIX_POSN_FROM_GAME_POSN(asteroids[asteroid]), COLOUR_BLACK);*/
	change_lives(-1);
	play_track(TRACK_ERROR);
	return 1;
}

/* Returns 1 if the base has a part at the given (x,y) position.
Returns 0 otherwise. Handles destroying the asteroid. */
static void check_all_base_hits() {
	check_base_hit(basePosition, 1);
	check_base_hit(basePosition-1, 0);
	check_base_hit(basePosition, 0);
	check_base_hit(basePosition+1, 0);
}

void advance_asteroids() {
	int8_t x, y, new_y;
	uint8_t i = 0;
	uint8_t j = 0;
	
	
	new_frame();
	set_display_attribute(TERM_RESET);
	s_invalidate_mode();
	/*uint8_t oldNumAsteroids = numAsteroids;*/
	while (i < numAsteroids) {
		j++;
		x = GET_X_POSITION(asteroids[i]);
		y = GET_Y_POSITION(asteroids[i]);
		
		// set current position to black.
		redraw_asteroid(i, COLOUR_BLACK); 
		
		new_y = y-1;
		if (new_y < 0) {
			remove_asteroid(i);
			continue;
		}
		if (check_asteroid_hit(projectile_at(x, new_y), i)) {
			s_invalidate_mode();
			continue;
		}
		
		asteroids[i] = GAME_POSITION(x, new_y);
		redraw_asteroid(i, COLOUR_ASTEROID);
		i++;
	}
	check_all_base_hits();
	
	/*redraw_all_asteroids();*/
	add_missing_asteroids();	
	draw_frame();
	redraw_base(COLOUR_BASE);
	
	#ifdef _ASTEROID_DEBUG
	_debug_asteroids();
	printf("advance_asteroids");
	#endif
}

// Returns 1 if the game is over, 0 otherwise. Initially, the game is
// never over.
int8_t is_game_over(void) {
	/*return 0;*/
	return (get_lives() == 0);
}

void set_paused(uint8_t pause) {
	paused = pause;
}

uint8_t is_paused() {
	return paused;
}
 
/******** INTERNAL FUNCTIONS ****************/

static void add_missing_asteroids(void) {
	s_invalidate_mode();
	for (uint8_t i = numAsteroids; i < MAX_ASTEROIDS; i++) {
		/*printf_P(PSTR("ADDING MISSING ASTEROID"));*/
		add_asteroid_in_rows(FIELD_HEIGHT-1);
	}
}

// Check whether there is an asteroid at a given position.
// Returns -1 if there is no asteroid, otherwise we return
// the asteroid number (from 0 to numAsteroids-1).
static int8_t asteroid_at(uint8_t x, uint8_t y) {
	uint8_t i;
	uint8_t positionToCheck = GAME_POSITION(x,y);
	for(i=0; i < numAsteroids; i++) {
		if(asteroids[i] == positionToCheck) {
			// Asteroid i is at the given position
			return i;
		}
	}
	// No match was found - no asteroid at the given position
	return -1;
}

// Check whether there is a projectile at a given position.
// Returns -1 if there is no projectile, otherwise we return
// the projectile number (from 0 to numProjectiles-1).
static int8_t projectile_at(uint8_t x, uint8_t y) {
	uint8_t i;
	uint8_t positionToCheck = GAME_POSITION(x,y);
	for(i=0; i < numProjectiles; i++) {
		if(projectiles[i] == positionToCheck) {
			// Projectile i is at the given position
			return i;
		}
	}
	// No match was found - no projectile at the given position 
	return -1;
}

/* Remove asteroid with the given index number (from 0 to
** numAsteroids - 1).
*/
static void remove_asteroid(int8_t asteroidNumber) {
	if (asteroidNumber < 0 || asteroidNumber >= numAsteroids) {
		// Invalid index - do nothing
		return;
	}
	
#ifdef _ASTEROID_DEBUG
	uint8_t x = GET_X_POSITION(asteroids[asteroidNumber]);
	uint8_t y = GET_Y_POSITION(asteroids[asteroidNumber]);
#endif
	
	// Remove the asteroid from the display
	redraw_asteroid(asteroidNumber, COLOUR_BLACK);
	
	// Close up the gap in the list of projectiles - move any
	// projectiles after this in the list closer to the start of the list
	for(uint8_t i = asteroidNumber+1; i < numAsteroids; i++) {
		asteroids[i-1] = asteroids[i];
	}
	/*asteroids[numAsteroids] = INVALID_POSITION;*/
	// Last position in asteroids array is no longer used
	numAsteroids--;
	
	/*add_asteroid_in_rows(FIELD_HEIGHT-1);*/

#ifdef _ASTEROID_DEBUG
	int8_t test = asteroid_at(x, y);
	if (test != -1) {
		printf("ASTEROID FAULT %d at (%d,%d)", test, x, y);
		_debug_asteroids();
		printf("fault ");
		while (button_pushed() == NO_BUTTON_PUSHED) {}
	}
	
	// Draw a new asteroid.
	// add_asteroid_in_rows(FIELD_HEIGHT-1);
	
	_debug_asteroids();
	printf("remove_asteroids");
#endif
}

// Remove projectile with the given projectile number (from 0 to
// numProjectiles - 1).
static void remove_projectile(int8_t projectileNumber) {	
	if(projectileNumber < 0 || projectileNumber >= numProjectiles) {
		// Invalid index - do nothing 
		return;
	}
	
	// Remove the projectile from the display
	redraw_projectile(projectileNumber, COLOUR_BLACK);
	
	// Close up the gap in the list of projectiles - move any
	// projectiles after this in the list closer to the start of the list
	for(uint8_t i = projectileNumber+1; i < numProjectiles; i++) {
		projectiles[i-1] = projectiles[i];
	}
	// Update projectile count - have one fewer projectiles now.
	numProjectiles--;
}

// Redraw the whole display - base, asteroids and projectiles.
// We assume all of the data structures have been appropriately poplulated
static void redraw_whole_display(void) {
	// clear the display
	ledmatrix_clear();
	
	// Redraw each of the elements
	redraw_base(COLOUR_BASE);
	redraw_all_asteroids();	
	redraw_all_projectiles();
}

static void redraw_base(uint8_t colour){
	// Add the bottom row of the base first (0) followed by the single bit
	// in the next row (1)
	for(int8_t x = basePosition - 1; x <= basePosition+1; x++) {
		if (x >= 0 && x < FIELD_WIDTH) {
			set_pixel(x, 0, colour);
// 			ledmatrix_update_pixel(LED_MATRIX_POSN_FROM_XY(x, 0), colour);
// 			draw_on_terminal(GAME_POSITION(x, 0), colour, '#');
		}
	}
	set_pixel(basePosition, 1, colour);
	print_terminal_buffer();
// 	ledmatrix_update_pixel(LED_MATRIX_POSN_FROM_XY(basePosition, 1), colour);
// 	draw_on_terminal(GAME_POSITION(basePosition, 1), colour, '#');
}

static void redraw_all_asteroids(void) {
	// For each asteroid, determine it's position and redraw it
	for(uint8_t i=0; i < numAsteroids; i++) {
		redraw_asteroid(i, COLOUR_ASTEROID);
	}
}

static void redraw_asteroid(uint8_t asteroidNumber, uint8_t colour) {
	uint8_t asteroidPosn;
	if(asteroidNumber < numAsteroids) {
		asteroidPosn = asteroids[asteroidNumber];
		set_pixel(GET_X_POSITION(asteroidPosn), GET_Y_POSITION(asteroidPosn), colour);
// 		ledmatrix_update_pixel(LED_MATRIX_POSN_FROM_GAME_POSN(asteroidPosn), colour);
// 		
// 		draw_on_terminal(asteroidPosn, colour, '@');
	}
}

static void redraw_all_projectiles(void){
	// For each projectile, determine its position and redraw it
	for(uint8_t i = 0; i < numProjectiles; i++) {
		redraw_projectile(i, COLOUR_PROJECTILE);
	}
}

static void redraw_projectile(uint8_t projectileNumber, uint8_t colour) {
	uint8_t projectilePosn;
	
	// Check projectileNumber is valid - ignore otherwise
	if(projectileNumber < numProjectiles) {
		projectilePosn = projectiles[projectileNumber];
		set_pixel(GET_X_POSITION(projectilePosn), GET_Y_POSITION(projectilePosn), colour);
// 		ledmatrix_update_pixel(LED_MATRIX_POSN_FROM_GAME_POSN(projectilePosn), colour);
// 		draw_on_terminal(projectilePosn, colour, 'o');
	}
}
