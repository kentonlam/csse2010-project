/*
 * terminalio.c
 *
 * Author: Peter Sutton
 */

#include <stdio.h>
#include <stdint.h>

#include <avr/pgmspace.h>

#include "terminalio.h"

DisplayParameter currentMode = TERM_RESET;
DisplayParameter sCurrentMode = TERM_RESET;

void move_cursor(int x, int y) {
    printf_P(PSTR("\x1b[%d;%dH"), y, x);
}

void s_invalidate_mode() {
	sCurrentMode = 50;
	currentMode = 50;
}


uint8_t s_move_cursor(char* arr, uint8_t x, uint8_t y){
	sprintf_P(arr, PSTR("\x1b[%d;%dH"), y, x);
	return 6 + (y>=10) + (x>=10);
}

uint8_t s_fast_set_display_attr(char* arr, DisplayParameter mode) {
	if (sCurrentMode == mode) {
		return 0;
	}
	sCurrentMode = mode;
	sprintf_P(arr, PSTR("\x1b[%dm"), mode);
	return 4 + (mode>= 10);
}

void normal_display_mode(void) {
	printf_P(PSTR("\x1b[0m"));
}

void reverse_video(void) {
	printf_P(PSTR("\x1b[7m"));
}

void clear_terminal(void) {
	printf_P(PSTR("\x1b[2J"));
}

void clear_to_end_of_line(void) {
	printf_P(PSTR("\x1b[K"));
}

void set_display_attribute(DisplayParameter parameter) {
	currentMode = parameter;
	printf_P(PSTR("\x1b[%dm"), parameter);
}

void hide_cursor() {
	printf_P(PSTR("\x1b[?25l"));
}

void show_cursor() {
	printf_P(PSTR("\x1b[?25h"));
}

void enable_scrolling_for_whole_display(void) {
	printf_P(PSTR("\x1b[r"));
}

void set_scroll_region(int8_t y1, int8_t y2) {
	printf_P(PSTR("\x1b[%d;%dr"), y1, y2);
}

void scroll_down(void) {
	printf_P(PSTR("\x1bM"));	// ESC-M
}

void scroll_up(void) {
	printf_P(PSTR("\x1b\x44"));	// ESC-D
}

void draw_horizontal_line(int8_t y, int8_t start_x, int8_t end_x) {
	int8_t i;
	move_cursor(start_x, y);
	reverse_video();
	for(i=start_x; i <= end_x; i++) {
		printf(" ");	/* No need to use printf_P - printing 
						 * a single character gets optimised
						 * to a putchar call 
						 */
	}
	normal_display_mode();
}

void draw_vertical_line(int8_t x, int8_t start_y, int8_t end_y) {
	int8_t i;
	move_cursor(x, start_y);
	reverse_video();
	for(i=start_y; i < end_y; i++) {
		printf(" ");
		/* Move down one and back to the left one */
		printf_P(PSTR("\x1b[B\x1b[D"));
	}
	printf(" ");
	normal_display_mode();
}

void draw_rectangle(uint8_t start_x, uint8_t start_y, 
	uint8_t width, uint8_t height) {
	int8_t i;
	reverse_video();
	move_cursor(start_x, start_y);
	for (i=0; i < width; i++) {
		printf(" "); // top line
	}
	printf("\b");
	for (i=0; i < height-1; i++) {
		printf(" ");
		/* Move down one and back to the left one */
		printf_P(PSTR("\x1b[B\x1b[D"));
	}
	printf(" ");
	move_cursor(start_x, start_y);
	for (i=0; i < height-1; i++) {
		printf(" ");
		/* Move down one and back to the left one */
		printf_P(PSTR("\x1b[B\x1b[D"));
	}
	printf(" ");
	printf("\b");
	for (i=0; i < width; i++) {
		printf(" "); // top line
	}
	normal_display_mode();		
}

void fast_set_display_attribute(DisplayParameter mode) {
	if (mode != currentMode) {
		set_display_attribute(mode);
		currentMode = mode;
	}
}