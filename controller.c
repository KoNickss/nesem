#include "controller.h"
#include "common.h"
#include <stdio.h>

typedef struct{
	byte state;
	byte new_state;
	byte cur_bit_mask;
}joypad_state_t;

static joypad_state_t jp[2] = {{0, 1}, {0, 1}};

#define JOYPAD_COUNT (sizeof(jp)/sizeof(joypad_state_t))

static inline void check_for_valid_jp(JOYPAD_t joypad_index){
	if(joypad_index >= JOYPAD_COUNT){
		fprintf(stderr, "ERR: Tried to read from joypad %d which does not exist!\n", joypad_index);
		abort();
	}	
}


bool joypad_read_bit(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);
	
	bool read_bit = !!(jp[joypad_index].state & jp[joypad_index].cur_bit_mask);
	jp[joypad_index].cur_bit_mask <<= 1;
	//Check if wrap around happens
	if(jp[joypad_index].cur_bit_mask == 0){
		jp[joypad_index].cur_bit_mask = 1;
	}
	return read_bit;
}

void joypad_zero_out(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);

	jp[joypad_index].state = 0;
	jp[joypad_index].new_state = 0;
	jp[joypad_index].cur_bit_mask = 1;
}

void joypad_prepare_read(void){
	for(int i = 0; i < JOYPAD_COUNT; i++){
		jp[i].cur_bit_mask = 0b1;
	}
}

//Fills the shift register and publishes the button states
void joypad_publish_state(void){
	for(int i = 0; i < JOYPAD_COUNT; i++){
		jp[i].state = jp[i].new_state;
		jp[i].new_state = 0;
	}
}


void joypad_set_button(JOYPAD_t joypad_index, BUTTON_t bit_index, bool button_state){
	check_for_valid_jp(joypad_index);

	if(bit_index >= 8){
		fprintf(stderr, "ERR: jpad bit index of %d is too high!\n", bit_index);
		abort();
	}

	#ifdef DEBUG
		switch(bit_index){
			case BUTTON_A:
				fprintf(stderr, "Pressed A Button\n");
			break;
			case BUTTON_B:
				fprintf(stderr, "Pressed B Button\n");
			break;
			case BUTTON_SELECT:
				fprintf(stderr, "Pressed Select Button\n");
			break;
			case BUTTON_START:
				fprintf(stderr, "Pressed Start Button\n");
			break;
			case BUTTON_UP:
				fprintf(stderr, "Presed Up Button\n");
			break;
			case BUTTON_DOWN:
				fprintf(stderr, "Pressed Down Button\n");
			break;
			case BUTTON_LEFT:
				fprintf(stderr, "Pressed Left Button\n");
			break;
			case BUTTON_RIGHT:
				fprintf(stderr, "Pressed Right Button\n");
			break;
		}

	#endif

	byte bval = ((byte)button_state) << bit_index;
	byte bmask = 1 << bit_index;
	jp[joypad_index].state &= ~bmask;
	jp[joypad_index].state |= bval;
}


