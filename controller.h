#ifndef CONTROLLER_H
#define CONTROLLER_H
#include "common.h"
#include "joystick.h"
#include <stdbool.h>

typedef enum{
	JOYPAD_1,
	JOYPAD_2
}JOYPAD_t;

typedef enum{
	CONTROLLER_MODE____INVALID,
	CONTROLLER_MODE_KEYBOARD,
	CONTROLLER_MODE_CONTROLLER,

	////////////////////////
	CONTROLLER_MODE____SIZE
}CONTROLLER_MODE_T;

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


bool joypad_plug_in_contoller(JOYPAD_t joypad_index, CONTROLLER_MODE_T controller_type, gpad_device_list_ent_t* device_id);

bool joypad_read_specific_button(JOYPAD_t joypad_index, BUTTON_t button);

byte joypad_read_bit(JOYPAD_t joypad_index);

void joypad_zero_out(JOYPAD_t joypad_index);

void joypad_set_button(JOYPAD_t joypad_index, BUTTON_t bit_index, bool button_state);

CONTROLLER_MODE_T joypad_get_joypad_mode(JOYPAD_t joypad_index);


//Sets the current bit to read to the first bit
void joypad_prepare_read(void);

//Fills the shift register and publishes the button states
void joypad_publish_state(void);

void joypad_disconnect(JOYPAD_t joypad_index);



#endif