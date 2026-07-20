#include "controller.h"
#include "common.h"
#include <stdio.h>



typedef struct{
	CONTROLLER_MODE_T control_mode;
	gpad_t controller_obj;
	byte state;
	byte cur_bit_mask;
	bool is_polled;
}joypad_state_t;

static joypad_state_t jp[2] = {
	{.control_mode=CONTROLLER_MODE____INVALID, .state=0, .cur_bit_mask=1, .is_polled=false}, 
	{.control_mode=CONTROLLER_MODE____INVALID, .state=0, .cur_bit_mask=1, .is_polled=false}, 
};

#define JOYPAD_COUNT (sizeof(jp)/sizeof(joypad_state_t))


#define check_for_valid_jp(joypad_index) SMART_ASSERT(joypad_index < JOYPAD_COUNT, "Tried to read from joypad %d which does not exist!\n", joypad_index);


bool joypad_plug_in_contoller(JOYPAD_t joypad_index, CONTROLLER_MODE_T controller_type, gpad_device_list_ent_t* device_id){
	check_for_valid_jp(joypad_index);

	if(controller_type == CONTROLLER_MODE____INVALID || controller_type >= CONTROLLER_MODE____SIZE){
		DERROR("INVALID CONTROLLER MODE! Controller mode was %d", controller_type);
		return false;
	}

	jp[joypad_index].control_mode = controller_type;
	if(controller_type == CONTROLLER_MODE_CONTROLLER){
		if(gpad_construct_from_device_list_ent(&jp[joypad_index].controller_obj, device_id) == false){
			PRINT_ERROR("controller", "Could not connect device \"%s\"\n", device_id->name);
			jp[joypad_index].control_mode = CONTROLLER_MODE____INVALID;
			jp[joypad_index].controller_obj.name = NULL;
			jp[joypad_index].controller_obj.fd=-1;	
		}
	}else{
		jp[joypad_index].controller_obj.name = NULL;
		jp[joypad_index].controller_obj.fd=-1;	
	}
	joypad_zero_out(joypad_index);
	jp[joypad_index].is_polled = false;
}


CONTROLLER_MODE_T joypad_get_joypad_mode(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);
	return jp[joypad_index].control_mode;
}

byte joypad_read_bit(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);

	if(jp[joypad_index].is_polled == false) return 0; //Dont do anything with the controller if we never recived a poll command
	
	bool read_bit = !!(jp[joypad_index].state & jp[joypad_index].cur_bit_mask);
	jp[joypad_index].cur_bit_mask <<= 1;
	//Check if wrap around happens
	if(jp[joypad_index].cur_bit_mask == 0){
		jp[joypad_index].cur_bit_mask = 1;
		jp[joypad_index].is_polled = false;
	}
	
	//Idk why but emulators always have 0x40 as well as the bit read
	return 0x40 | (byte)read_bit;
}

void joypad_zero_out(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);

	jp[joypad_index].state = 0;
	jp[joypad_index].cur_bit_mask = 1;
}

void joypad_prepare_read(void){
	for(int i = 0; i < JOYPAD_COUNT; i++){
		jp[i].cur_bit_mask = 0b1;
	}
}


static void _switch_controller_to_keyboard_input(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);

	joypad_state_t* cur_jp = &jp[joypad_index];
	gpad_t* gp = &cur_jp->controller_obj;

	PRINT_INFO("controller", "Controller \"%s\" in Joypad slot #%i has disconnected! Switching to keyboard input", gp->name, joypad_index);
	gpad_t_free(gp);
	gp = NULL;
	cur_jp->control_mode = CONTROLLER_MODE_KEYBOARD;
	cur_jp->state = 0;
}

bool joypad_read_specific_button(JOYPAD_t joypad_index, BUTTON_t button){
	byte bmask = 1 << button;
	return jp[joypad_index].state & bmask;
}

static inline void _debug_print_joypad_state(JOYPAD_t joypad_index){
	const char* bstates[] = {
		joypad_read_specific_button(joypad_index, BUTTON_A) ? "true" : "false",
		joypad_read_specific_button(joypad_index, BUTTON_B) ? "true" : "false",
		joypad_read_specific_button(joypad_index, BUTTON_START) ? "true" : "false",
		joypad_read_specific_button(joypad_index, BUTTON_SELECT) ? "true" : "false",
		joypad_read_specific_button(joypad_index, BUTTON_LEFT) ? "true" : "false",
		joypad_read_specific_button(joypad_index, BUTTON_RIGHT) ? "true" : "false",
		joypad_read_specific_button(joypad_index, BUTTON_UP) ? "true" : "false",
		joypad_read_specific_button(joypad_index, BUTTON_DOWN) ? "true" : "false",
	};


	fprintf(stderr, "A: %s B: %s St: %s Sl: %s l: %s r: %s u: %s d: %s\n", bstates[0],bstates[1],bstates[2],bstates[3],bstates[4],bstates[5],bstates[6],bstates[7]);
}

static void _poll_controller_state(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);

	joypad_state_t* cur_jp = &jp[joypad_index];
	gpad_t* gp = &cur_jp->controller_obj;

	if(gpad_read(gp) != READ_OK){
		_switch_controller_to_keyboard_input(joypad_index);
		return;
	}

	float DEADZONE_STICK = 0.4;
	word START_MASK = 0b1000000000;
	word SELECT_MASK = 0b100000000;
	word A_MASK = 0b1;
	word B_MASK = 0b1000;
	word DPAD_AXIS = 3;
	word LSTICK_AXIS = 0;
	size_t DPAD_DOWN_MASK = 0;
	size_t DPAD_UP_MASK = 0;
	size_t DPAD_LEFT_MASK = 0;
	size_t DPAD_RIGHT_MASK = 0;
	bool left_pressed = false;
	bool right_pressed = false;
	bool up_pressed = false;
	bool down_pressed = false;

	//Gpad is in valid state
	switch(gp->brand){
		case GPAD_CON_SONY:
			switch(gp->model.sony){
				case GPAD_CON_MODEL_SONY_PS5:
					DEADZONE_STICK = 0.4;
					START_MASK = 0b1000000000;
					SELECT_MASK = 0b100000000;
					A_MASK = 0b1;
					B_MASK = 0b1000;
					DPAD_AXIS = 3;
					LSTICK_AXIS = 0;

					joypad_set_button(joypad_index, BUTTON_START, gp->buttons & START_MASK);
					joypad_set_button(joypad_index, BUTTON_SELECT, gp->buttons & SELECT_MASK);
					joypad_set_button(joypad_index, BUTTON_A, gp->buttons & A_MASK);
					joypad_set_button(joypad_index, BUTTON_B, gp->buttons & B_MASK);

					left_pressed = gp->axis[DPAD_AXIS].x <= (DEADZONE_STICK * -1.0f) || gp->axis[LSTICK_AXIS].x <= (DEADZONE_STICK * -1.0f);
					right_pressed = gp->axis[DPAD_AXIS].x >= (DEADZONE_STICK * 1.0f) || gp->axis[LSTICK_AXIS].x >= (DEADZONE_STICK * 1.0f);
					up_pressed = gp->axis[DPAD_AXIS].y <= (DEADZONE_STICK * -1.0f) || gp->axis[LSTICK_AXIS].y <= (DEADZONE_STICK * -1.0f);
					down_pressed = gp->axis[DPAD_AXIS].y >= (DEADZONE_STICK * 1.0f) || gp->axis[LSTICK_AXIS].y >= (DEADZONE_STICK * 1.0f);

					joypad_set_button(joypad_index, BUTTON_DOWN, down_pressed);
					joypad_set_button(joypad_index, BUTTON_UP, up_pressed);
					joypad_set_button(joypad_index, BUTTON_RIGHT, right_pressed);
					joypad_set_button(joypad_index, BUTTON_LEFT,left_pressed);

					
				break;
				default:
					PRINT_ERROR("controller", "Unknown Sony controller!");
					_switch_controller_to_keyboard_input(joypad_index);
					return;
				break;
			}
		break;
		case GPAD_CON_XBOX:
			switch(gp->model.xbox){
				case GPAD_CON_MODEL_XBOX_360:
					DEADZONE_STICK = 0.4;
					START_MASK = 0b10000000;
					SELECT_MASK = 0b1000000;
					A_MASK = 0b1;
					B_MASK = 0b100;
					DPAD_AXIS = 3;
					LSTICK_AXIS = 0;

					joypad_set_button(joypad_index, BUTTON_START, gp->buttons & START_MASK);
					joypad_set_button(joypad_index, BUTTON_SELECT, gp->buttons & SELECT_MASK);
					joypad_set_button(joypad_index, BUTTON_A, gp->buttons & A_MASK);
					joypad_set_button(joypad_index, BUTTON_B, gp->buttons & B_MASK);

					left_pressed = gp->axis[DPAD_AXIS].x <= (DEADZONE_STICK * -1.0f) || gp->axis[LSTICK_AXIS].x <= (DEADZONE_STICK * -1.0f);
					right_pressed = gp->axis[DPAD_AXIS].x >= (DEADZONE_STICK * 1.0f) || gp->axis[LSTICK_AXIS].x >= (DEADZONE_STICK * 1.0f);
					up_pressed = gp->axis[DPAD_AXIS].y <= (DEADZONE_STICK * -1.0f) || gp->axis[LSTICK_AXIS].y <= (DEADZONE_STICK * -1.0f);
					down_pressed = gp->axis[DPAD_AXIS].y >= (DEADZONE_STICK * 1.0f) || gp->axis[LSTICK_AXIS].y >= (DEADZONE_STICK * 1.0f);

					joypad_set_button(joypad_index, BUTTON_DOWN, down_pressed);
					joypad_set_button(joypad_index, BUTTON_UP, up_pressed);
					joypad_set_button(joypad_index, BUTTON_RIGHT, right_pressed);
					joypad_set_button(joypad_index, BUTTON_LEFT,left_pressed);
				break;
				default:
					PRINT_ERROR("controller", "Unknown XBOX controller!");
					_switch_controller_to_keyboard_input(joypad_index);
				break;
			}
		break;
		case GPAD_CON_NINTENDO:
			switch(gp->model.nintendo){
				case GPAD_CON_MODEL_NINTENDO_WII_U_PRO_CONTROLLER:
					DEADZONE_STICK = 0.4;
					START_MASK = 0b1000000000;
					SELECT_MASK = 0b100000000;
					A_MASK = 0b1;
					B_MASK = 0b1000;
					LSTICK_AXIS = 0;

					DPAD_DOWN_MASK = 0b1 << 14;
					DPAD_UP_MASK = 0b1 << 13;
					DPAD_LEFT_MASK = 0b1 << 15;
					DPAD_RIGHT_MASK = 0b1 << 16;



					joypad_set_button(joypad_index, BUTTON_START, gp->buttons & START_MASK);
					joypad_set_button(joypad_index, BUTTON_SELECT, gp->buttons & SELECT_MASK);
					joypad_set_button(joypad_index, BUTTON_A, gp->buttons & A_MASK);
					joypad_set_button(joypad_index, BUTTON_B, gp->buttons & B_MASK);

					left_pressed =  (gp->buttons & DPAD_LEFT_MASK) || gp->axis[LSTICK_AXIS].x <= (DEADZONE_STICK * -1.0f);
					right_pressed = (gp->buttons & DPAD_RIGHT_MASK) || gp->axis[LSTICK_AXIS].x >= (DEADZONE_STICK * 1.0f);
					up_pressed =    (gp->buttons & DPAD_UP_MASK) || gp->axis[LSTICK_AXIS].y <= (DEADZONE_STICK * -1.0f);
					down_pressed =  (gp->buttons & DPAD_DOWN_MASK) || gp->axis[LSTICK_AXIS].y >= (DEADZONE_STICK * 1.0f);

					joypad_set_button(joypad_index, BUTTON_DOWN, down_pressed);
					joypad_set_button(joypad_index, BUTTON_UP, up_pressed);
					joypad_set_button(joypad_index, BUTTON_RIGHT, right_pressed);
					joypad_set_button(joypad_index, BUTTON_LEFT,left_pressed);
				break;
				case GPAD_CON_MODEL_NINTENDO_WII_MOTE:
					START_MASK = 0b1 << (8);
					SELECT_MASK = 0b1 << (9);
					A_MASK = (0b1 << (6)) | (0b1 << (5));
					B_MASK = (0b1 << (7)) | (0b1 << (4));

					DPAD_DOWN_MASK = 0b1 << (1);
					DPAD_UP_MASK = 0b1;
					DPAD_LEFT_MASK = 0b1 << (2);
					DPAD_RIGHT_MASK = 0b1 << (3);



					joypad_set_button(joypad_index, BUTTON_START, gp->buttons & START_MASK);
					joypad_set_button(joypad_index, BUTTON_SELECT, gp->buttons & SELECT_MASK);
					joypad_set_button(joypad_index, BUTTON_A, gp->buttons & A_MASK);
					joypad_set_button(joypad_index, BUTTON_B, gp->buttons & B_MASK);

					left_pressed =  (gp->buttons & DPAD_LEFT_MASK);
					right_pressed = (gp->buttons & DPAD_RIGHT_MASK);
					up_pressed =    (gp->buttons & DPAD_UP_MASK);
					down_pressed =  (gp->buttons & DPAD_DOWN_MASK);

					joypad_set_button(joypad_index, BUTTON_DOWN, down_pressed);
					joypad_set_button(joypad_index, BUTTON_UP, up_pressed);
					joypad_set_button(joypad_index, BUTTON_RIGHT, right_pressed);
					joypad_set_button(joypad_index, BUTTON_LEFT,left_pressed);

				break;
				default:
					PRINT_ERROR("controller", "Unknown Nintendo controller!");
					_switch_controller_to_keyboard_input(joypad_index);
				break;
			}
		break;
		default:
			PRINT_ERROR("controller", "Unknown Brand controller!");
			_switch_controller_to_keyboard_input(joypad_index);
		break;
	}
} 

//Fills the shift register and publishes the button states
void joypad_publish_state(void){
	for(int i = 0; i < JOYPAD_COUNT; i++){
		if(joypad_get_joypad_mode(i) == CONTROLLER_MODE_CONTROLLER){
			_poll_controller_state(i);
		}

		jp[i].is_polled = true;
	}
}


void joypad_set_button(JOYPAD_t joypad_index, BUTTON_t bit_index, bool button_state){
	check_for_valid_jp(joypad_index);

	SMART_ASSERT(bit_index < 8, "jpad #%i attempted to set bit index of %d but that is OOB!", joypad_index, bit_index);

	#ifdef DEBUG
	if(button_state != joypad_read_specific_button(joypad_index, bit_index)){
		if(button_state == true){
			fprintf(stderr, "Pressed ");
		}else{
			fprintf(stderr, "Released ");
		}
		switch(bit_index){
			case BUTTON_A:
				fprintf(stderr, "A Button\n");
			break;
			case BUTTON_B:
				fprintf(stderr, "B Button\n");
			break;
			case BUTTON_SELECT:
				fprintf(stderr, "Select Button\n");
			break;
			case BUTTON_START:
				fprintf(stderr, "Start Button\n");
			break;
			case BUTTON_UP:
				fprintf(stderr, "Up Button\n");
			break;
			case BUTTON_DOWN:
				fprintf(stderr, "Down Button\n");
			break;
			case BUTTON_LEFT:
				fprintf(stderr, "Left Button\n");
			break;
			case BUTTON_RIGHT:
				fprintf(stderr, "Right Button\n");
			break;
		}
	}

	#endif

	byte bval = ((byte)button_state) << bit_index;
	byte bmask = 1 << bit_index;
	jp[joypad_index].state &= ~bmask;
	jp[joypad_index].state |= bval;
}


void joypad_disconnect(JOYPAD_t joypad_index){
	check_for_valid_jp(joypad_index);

	switch(joypad_get_joypad_mode(joypad_index)){
		case CONTROLLER_MODE_CONTROLLER:
			gpad_t_free(&jp[joypad_index].controller_obj);
		break;
		case CONTROLLER_MODE_KEYBOARD:
		case CONTROLLER_MODE____INVALID:
		break;
		default:
			DERROR("Could not delete controller with unknown type %d", joypad_get_joypad_mode(joypad_index));
		break;
	}
	jp[joypad_index].control_mode = CONTROLLER_MODE____INVALID;
}