#ifndef CONTROLLER_H
#define CONTROLLER_H
#include "common.h"
#include <stdbool.h>

typedef enum{
	JOYPAD_1,
	JOYPAD_2
}JOYPAD_t;

#if 0
typedef enum{
	BUTTON_RIGHT,
	BUTTON_LEFT,
	BUTTON_DOWN,
	BUTTON_UP,
	BUTTON_START,
	BUTTON_SELECT,
	BUTTON_B,
	BUTTON_A
}BUTTON_t;
#else
typedef enum{
	BUTTON_A,
	BUTTON_B,
	BUTTON_SELECT,
	BUTTON_START,
	BUTTON_UP,
	BUTTON_DOWN,
	BUTTON_LEFT,
	BUTTON_RIGHT,
}BUTTON_t;
#endif


bool joypad_read_bit(JOYPAD_t joypad_index);

void joypad_zero_out(JOYPAD_t joypad_index);

void joypad_set_button(JOYPAD_t joypad_index, BUTTON_t bit_index, bool button_state);









#endif