#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/joystick.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include "joystick.h"
#include "common.h"

//https://www.kernel.org/doc/Documentation/input/joystick-api.txt

typedef unsigned int u32;
typedef short s16;
typedef unsigned char u8;

typedef struct js_event controller_event;

typedef u32 js_t;


#include <pthread.h>
#include <fcntl.h>
#include <errno.h>


#include <dirent.h>


static inline void _smart_close(int fd, const char* _func, const char* _file, size_t _line){
	if(close(fd) != 0){
		PRINT_ERROR("gpad", "Could not close fd=%i in func \"%s\" in file \"%s\" on line %lu! ", fd, _func, _file, _line);
		perror("");
		abort();
	}
}
#define smart_close(fd) _smart_close(fd, __PRETTY_FUNCTION__, __FILE__, __LINE__)




//Checks if string A ends with string B
static inline bool _string_ends_with(const char* target_str, const char* search_str){
	const size_t STRING_MAX_SIZE = PATH_MAX; //Chosen to avoid infinite reads
	size_t target_strlen = strnlen(target_str, STRING_MAX_SIZE);
	size_t search_strlen = strnlen(search_str, STRING_MAX_SIZE);

	if(search_strlen > target_strlen){
		return false;
	}
	if(search_strlen == 0 || target_strlen == 0){
		return false;
	}

	const char* target_search_start = target_str + target_strlen - search_strlen;

	return (strncmp(target_search_start, search_str, search_strlen) == 0);
}


void gpad_t_free(gpad_t* gpad){
	if(gpad->axis != NULL)
		free(gpad->axis);
	if(gpad->name != NULL)
		free(gpad->name);
	if(gpad->events != NULL)
		free(gpad->events);
	if(gpad->fd >= 0){
		smart_close(gpad->fd);
	}
	memset(gpad, 0, sizeof(gpad_t));
	gpad->axis_count = 0;
	gpad->button_count = 0;

	gpad->axis = NULL;
	gpad->name = NULL;
	gpad->events = NULL;
	
	gpad->fd = -1;
	gpad->rumble_event = -1;
	gpad->connection_mode = GPAD_PROTOCOL_MODE_INVALID;
}


static int _gpad_read_joydev(gpad_t* gpad){
	controller_event e;
	while(read(gpad->fd, &e, sizeof(e)) > 0){
		//if(e.type & JS_EVENT_INIT) continue;

		if((e.type & JS_EVENT_BUTTON)){
			if(gpad->button_count > 0){
				if (e.value){
					gpad->buttons |= (1u << e.number);
				}else{
					gpad->buttons &= ~(1u << e.number);
				}
			}else{
				DWARN("Button event was sent but there are no buttons on the controller!");
			}
			SMART_WARN(e.number <= gpad->button_count, "Button was pressed but it is a button that is more than the controller supports! Pressed Button index = %i, button_count=%lu", e.number, gpad->button_count);
		}
		if(e.type & JS_EVENT_AXIS){
			if(gpad->axis_count > 0){
				gpad->axis[e.number / 2].arrary[e.number % 2] = ((float)e.value) / 32767.0f;
			}else{
				DWARN("Axis event was sent but there are no axises on the controller!");
			}
		}
	}
	if(errno != EAGAIN){
		PRINT_ERROR("controller", "Error reading controller device!");
		DPERROR("Controller \"%s\" could not be read! reason=", gpad->name);
		smart_close(gpad->fd);
		gpad->fd = -1;
		return READ_FAILED;
	}

	return READ_OK;
}

typedef ssize_t button_mapping_t;


typedef enum{
	GPAD_ORDINAL_AXIS_X_INDEX,
	GPAD_ORDINAL_AXIS_Y_INDEX,
}GPAD_ORDINAL_AXIS_INDEX_t;

typedef struct{
	ssize_t x_axis;
	ssize_t y_axis;
}hat_t;

typedef struct{
	ssize_t stick_idx;
	GPAD_ORDINAL_AXIS_INDEX_t oridinal_axis;
	int min_value;
	int max_value;
}stick_t;

typedef struct{
	hat_t hat[4];
	stick_t abs_x;
	stick_t abs_y;
	stick_t abs_z;
	stick_t abs_rx;
	stick_t abs_ry;
	stick_t abs_rz;
}axis_mapping_t;


typedef struct{
	const button_mapping_t* button_mapping;
	const axis_mapping_t* axis_mapping;
}evdev_mapping_t;

#include <stdint.h>

static const evdev_mapping_t* _get_evdev_mapping(gpad_t* gpad){
	switch(gpad->brand){
		case GPAD_CON_NINTENDO:
			switch(gpad->model.nintendo){
				case GPAD_CON_MODEL_NINTENDO_WII_MOTE:
					static const button_mapping_t WIIMOTE_BUTTON_MAPPING[10] = {
						106, //Up
						105, //down
						103, //left
						108, //right
						257, //1
						258, //2
						304, //A
						305, //B
						407, //Start
						412, //Minus
					};
					static const evdev_mapping_t WIIMOTE_MAPPING = {
						.button_mapping = WIIMOTE_BUTTON_MAPPING,
						.axis_mapping = NULL
					};

					gpad->button_count = sizeof(WIIMOTE_BUTTON_MAPPING)/sizeof(ssize_t);
					
					return &WIIMOTE_MAPPING;
				break;
				case GPAD_CON_MODEL_NINTENDO_WII_U_PRO_CONTROLLER:
					static const ssize_t WIIU_PRO_BUTTON_MAPPING[16] = {
						BTN_SOUTH, //B
						BTN_EAST, //A
						BTN_NORTH, //X
						BTN_WEST, //Y
						0x136, //L
						0x137, //R
						0x138, //ZL
						0x139, //ZR
						0x13A, //(select)
						0x13B, //(start)
						0x13D, //L Stick press down
						0x13E, //R Stick press down
						0x220, //u dpad
						0x221, //d dpad
						0x222, //l dpad
						0x223, //r dpad
					};
					static const axis_mapping_t WIIU_PRO_AXIS_MAPPING = {
						.hat = {
							{-1, -1},
							{-1, -1},
							{-1, -1},
							{-1, -1}
						},
						.abs_x = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=-1280,
							.max_value=1280
						},
						.abs_y = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=-1280,
							.max_value=1280
						},
						.abs_z = {
							.stick_idx = -1,
						},
						.abs_rx = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=-1280,
							.max_value=1280
						},
						.abs_ry = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=-1280,
							.max_value=1280
						},
						.abs_rz = {
							.stick_idx = -1,
						},
					};
					static const evdev_mapping_t WIIU_PRO_MAPPING = {
						.button_mapping = WIIU_PRO_BUTTON_MAPPING,
						.axis_mapping = &WIIU_PRO_AXIS_MAPPING
					};

					return &WIIU_PRO_MAPPING;
				break;
				case GPAD_CON_MODEL_NINTENDO_SWITCH_PRO:
					static const ssize_t SWITCH_PRO_BUTTON_MAPPING[16] = {
						BTN_SOUTH, //B
						BTN_EAST, //A
						BTN_NORTH, //X
						BTN_WEST, //Y
						0x136, //L
						0x137, //R
						0x138, //ZL
						0x139, //ZR
						0x13A, //(select)
						0x13B, //(start)
						0x13D, //L Stick press down
						0x13E, //R Stick press down
						0x220, //u dpad
						0x221, //d dpad
						0x222, //l dpad
						0x223, //r dpad
					};
					static const axis_mapping_t SWITCH_PRO_AXIS_MAPPING = {
						.hat = {
							{0, 0},
							{-1, -1},
							{-1, -1},
							{-1, -1}
						},
						.abs_x = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=-32767,
							.max_value=32767
						},
						.abs_y = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=-32767,
							.max_value=32767
						},
						.abs_z = {
							.stick_idx = -1,
						},
						.abs_rx = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=-32767,
							.max_value=32767
						},
						.abs_ry = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=-32767,
							.max_value=32767
						},
						.abs_rz = {
							.stick_idx = -1,
						},
					};
					static const evdev_mapping_t SWITCH_PRO_MAPPING = {
						.button_mapping = SWITCH_PRO_BUTTON_MAPPING,
						.axis_mapping = &SWITCH_PRO_AXIS_MAPPING
					};

					return &SWITCH_PRO_MAPPING;
				break;
				default:
					DERROR("Not a mapped Nintendo controller (controller model = %u)!", gpad->model.nintendo);
					return NULL;
				break;
			};
		break;
		case GPAD_CON_SONY:
			switch(gpad->model.sony){
				case GPAD_CON_MODEL_SONY_PS5:
					static const ssize_t SONY_PS5_BUTTON_MAPPING[15] = {
						0x130, //X
						0x131, //O
						0x133, //Tri
						0x134, //Square
						0x136, //L1
						0x137, //R1
						0x138, //L2
						0x139, //R2
						0x13A, //Light (select)
						0x13B, //Three lines (start)
						0x13D, //L Stick press down
						0x13E, //R Stick press down
					};
					static const axis_mapping_t SONY_PS5_AXIS_MAPPING = {
						.hat = {
							{0, 0},
							{-1, -1},
							{-1, -1},
							{-1, -1}
						},
						.abs_x = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=0,
							.max_value=255
						},
						.abs_y = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=0,
							.max_value=255
						},
						.abs_z = {
							.stick_idx = -1,
						},
						.abs_rx = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=0,
							.max_value=255
						},
						.abs_ry = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=0,
							.max_value=255
						},
						.abs_rz = {
							.stick_idx = -1,
						},
					};
					static const evdev_mapping_t SONY_PS5_MAPPING = {
						.button_mapping = SONY_PS5_BUTTON_MAPPING,
						.axis_mapping = &SONY_PS5_AXIS_MAPPING
					};

					return &SONY_PS5_MAPPING;
				break;
				default:
					DERROR("Not a mapped Sony controller (controller model = %u)!", gpad->model.sony);
					return NULL;
				break;
			};
		break;
		case GPAD_CON_XBOX:
			switch(gpad->model.xbox){
				case GPAD_CON_MODEL_XBOX_360:
					//the spaces with -1 are to emulate joydev layout
					static const ssize_t XBOX_360_BUTTON_MAPPING[13] = {
						0x130, //A
						0x131, //B
						0x133, //Y
						0x134, //X
						0x136, //LB
						0x137, //RB
						0x13A, //select
						0x13B, //start
						0x13D, //L Stick press down
						0x13E, //R Stick press down
					};
					static const axis_mapping_t XBOX_360_AXIS_MAPPING = {
						.hat = {
							{0, 0},
							{-1, -1},
							{-1, -1},
							{-1, -1}
						},
						.abs_x = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=INT16_MIN,
							.max_value=INT16_MAX
						},
						.abs_y = {
							.stick_idx = 1,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=INT16_MIN,
							.max_value=INT16_MAX
						},
						.abs_z = {
							.stick_idx = -1,
						},
						.abs_rx = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_X_INDEX,
							.min_value=INT16_MIN,
							.max_value=INT16_MAX
						},
						.abs_ry = {
							.stick_idx = 2,
							.oridinal_axis = GPAD_ORDINAL_AXIS_Y_INDEX,
							.min_value=INT16_MIN,
							.max_value=INT16_MAX
						},
						.abs_rz = {
							.stick_idx = -1,
						},
					};
					static const evdev_mapping_t XBOX_360_MAPPING = {
						.button_mapping = XBOX_360_BUTTON_MAPPING,
						.axis_mapping = &XBOX_360_AXIS_MAPPING
					};

					return &XBOX_360_MAPPING;
				break;
				default:
					DERROR("Not a mapped XBOX controller (controller model = %u)!", gpad->model.xbox);
					return NULL;
				break;
			};
		break;
		default:
			DERROR("Not a mapped controller (brand = %u, model = %u)!", gpad->brand, gpad->model.nintendo);
			return NULL;
		break;
	};

	return NULL;
}




#include <linux/input-event-codes.h>
static int _gpad_read_evdev(gpad_t* gpad){
	struct input_event ev;
	
	const evdev_mapping_t* evdev_mapping = _get_evdev_mapping(gpad);
	if(evdev_mapping == NULL){
		DERROR("Could not get mapping!");
		return READ_FAILED;
	}

	while (read(gpad->fd, &ev, sizeof(ev)) == sizeof(ev)) {

		switch (ev.type) {
			case EV_SYN:
				if(ev.code != 0 || ev.value != 0){
					DWARN("UNKNOWN COMMAND type=%X code=%X value=%X", ev.type, ev.code, ev.value);
				}
			break;
			case EV_KEY:

				button_mapping_t button_index = -1;
				PRINT_INFO("evdev", "C%X, V%X", ev.code, ev.value);
				
				for(ssize_t i = 0; i < gpad->button_count; i++){
					if(ev.code == evdev_mapping->button_mapping[i]){
						button_index = i;
						break;
					}
				}

				if(button_index >= 0){	
					//Set the proper bit
					if (ev.value){
						gpad->buttons |= (1u << button_index);
					}else{
						gpad->buttons &= ~(1u << button_index);
					}
				}else{
					DERROR("Unknown code 0x%X, with value 0x%X", ev.code, ev.value);
				}


			break;
			case EV_ABS:
				const size_t AXIS_INDEX_X = 0;
				const size_t AXIS_INDEX_Y = 1;
				ssize_t stick = -1;
				size_t axis_index = 0;
				const stick_t* stick_ptr = NULL;

				float value = ev.value;

				switch(ev.code){
					case ABS_HAT0X:
						stick = evdev_mapping->axis_mapping->hat[0].x_axis;
						axis_index = AXIS_INDEX_X;
					break;
					case ABS_HAT0Y:
						stick = evdev_mapping->axis_mapping->hat[0].y_axis;
						axis_index = AXIS_INDEX_Y;
					break;
					case ABS_HAT1X:
						stick = evdev_mapping->axis_mapping->hat[1].x_axis;
						axis_index = AXIS_INDEX_X;
					break;
					case ABS_HAT1Y:
						stick = evdev_mapping->axis_mapping->hat[1].y_axis;
						axis_index = AXIS_INDEX_Y;
					break;
					case ABS_HAT2X:
						stick = evdev_mapping->axis_mapping->hat[2].x_axis;
						axis_index = AXIS_INDEX_X;
					break;
					case ABS_HAT2Y:
						stick = evdev_mapping->axis_mapping->hat[2].y_axis;
						axis_index = AXIS_INDEX_Y;
					break;
					case ABS_HAT3X:
						stick = evdev_mapping->axis_mapping->hat[3].x_axis;
						axis_index = AXIS_INDEX_X;
					break;
					case ABS_HAT3Y:
						stick = evdev_mapping->axis_mapping->hat[3].y_axis;
						axis_index = AXIS_INDEX_Y;
					break;
					case ABS_X:
						stick_ptr =  &evdev_mapping->axis_mapping->abs_x;
					break;
					case ABS_Y:
						stick_ptr =  &evdev_mapping->axis_mapping->abs_y;
					break;
					case ABS_Z:
						stick_ptr =  &evdev_mapping->axis_mapping->abs_z;
					break;
					case ABS_RX:
						stick_ptr =  &evdev_mapping->axis_mapping->abs_rx;
					break;
					case ABS_RY:
						stick_ptr =  &evdev_mapping->axis_mapping->abs_ry;
					break;
					case ABS_RZ:
						stick_ptr =  &evdev_mapping->axis_mapping->abs_rz;
					break;
					default:
						DERROR("Unsupported ABS command! %X:%X:%X", ev.type, ev.code, ev.value);
					break;
				};


				//Check for real actual stick (not hat) to do some normalization
				if(stick_ptr != NULL){
					stick = stick_ptr->stick_idx;
					axis_index = stick_ptr->oridinal_axis;

					int64_t ev_value_new = ((int64_t)ev.value) - ((int64_t)stick_ptr->min_value);

					value = ((float)ev_value_new)/((float)(stick_ptr->max_value - stick_ptr->min_value)); //(0.0f, 1.0f)
					value = (value * 2.0f) - 1.0f; //Min value should be negative (-1.0f, 1.0f)
					
				}

				if(stick >= 0){
					gpad->axis[stick].arrary[axis_index] = value;
				}
				

			break;
			default:
				DERROR("UNKNOW EVDEV COMMAND %X:%X:%X", ev.type, ev.code, ev.value);
			break;
		}
	}

	return READ_OK;
}

int gpad_read(gpad_t* gpad){

	if(gpad->fd < 0){
		return READ_DEAD_CONTROLLER;
	}

	switch((unsigned int)gpad->connection_mode){
		case GPAD_PROTOCOL_MODE_JOYDEV:
			return _gpad_read_joydev(gpad);
		break;
		case GPAD_PROTOCOL_MODE_EVDEV:
			return _gpad_read_evdev(gpad);
		break;
		default:
			DERROR("Invalid controller protocol %u!", (unsigned int)gpad->connection_mode);
			return READ_FAILED;
		break;
	}

	return READ_FAILED;
}


static inline const char* _get_protocol_string(GPAD_PROTOCOL_MODE_T protocol){
	char* protocol_str = NULL;
	switch(protocol){
		case GPAD_PROTOCOL_MODE_JOYDEV:
			protocol_str = "js";
		break;
		case GPAD_PROTOCOL_MODE_EVDEV:
			protocol_str = "event";
		break;
		default:
			DERROR("Bad protocol %i!", (int)protocol);
			abort();
		break;
	};
	SMART_ASSERT(protocol_str != NULL, "Protocol string was NULL! This should never happen");
	return protocol_str;
} 



typedef struct{
	u_int16_t vendor_id;
	u_int16_t product_id;
}vendor_data_t;


static u_int16_t _get_uint16_file(const char file_path[], js_t js){
	const char* buf2 = file_path;
	FILE* f = fopen(buf2, "r");
	if(f == NULL){
		DPERROR("Could not open VENDOR ID for controller %u  ", js);
		return 0;
	}
	char name[5] = "    ";
	fread(name, 4, 1, f);
	fclose(f);
	char* end;
	u_int16_t ret = strtoul(name, &end, 16);
	if (name == end){
		return 0;
	}
	return ret;
}


static int get_events(js_t js, event_t events[], int events_len, GPAD_PROTOCOL_MODE_T protocol){
	if(events_len <= 0){
		return -1;
	}
	
	const char* protocol_str = _get_protocol_string(protocol);


	int real_event_len = 0;
	char* path = (char*)xmalloc(PATH_MAX);
	path[0] = 0;
	path[PATH_MAX-1] = 0;
	int a = snprintf(path, PATH_MAX, "/sys/class/input/%s%u/device", protocol_str, js);
	if(a <= strlen("/sys/class/input/") + strlen(protocol_str)){
		free(path);
		return -1;
	}
	
	DIR *dir = opendir(path);
    if (!dir) {
        DPERROR("Could not get events for js=%i", js);
		free(path);
        return -1;
    }

    struct dirent *entry;

    // Look for eventX inside js0 device directory
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "event", strlen("event")) == 0) {
			a = sscanf(entry->d_name + strlen("event"), "%lli", &events[real_event_len++]);
			if(a < 1){
				free(path);
				return -1;
			}
			if(real_event_len >= events_len){
				free(path);
				return real_event_len;
			}

            //break;
        }
    }

    closedir(dir);

	free(path);
	return real_event_len;
}


#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#endif

#define BITS_TO_LONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)

static int test_bit(int bit, unsigned long *array)
{
    return (array[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1;
}


static vendor_data_t _get_vendor_data(js_t js, GPAD_PROTOCOL_MODE_T protocol){

	#define ENABLE_HIDRAW false //Requires root
	#define GET_VENDOR_INFO_FROM_HW_DEVICE false //Gets the vendor id from the interface. This is bad if you are using bluetooth because this gets the vendor info for the bluetooth device

	#if ENABLE_HIDRAW
	char* device_path_buffer = (char*)alloca(PATH_MAX);
	vendor_data_t dat;
	dat.vendor_id = 0;
	dat.product_id = 0;
	if(js > 99){
		return dat;
	}
	
	sprintf(device_path_buffer, "/sys/class/input/js%u", js);
	
	char buf[PATH_MAX];
	ssize_t len = readlink(device_path_buffer, buf, sizeof(buf));
	if(len <= 0){
		return dat;
	}
	if (len != -1) {
		buf[len] = '\0';
	}

	strcat(device_path_buffer, "/device");

	char* buf2= device_path_buffer; //Sneaky re-use of memory
	buf2[0] = 0;
	buf2[PATH_MAX-1] =0;
	len = snprintf(buf2, PATH_MAX, "%s/%s", "/sys/class/input", buf);

	while(true){
		strncat(buf2, "/idVendor", PATH_MAX - len - 1);
		printf("Checking \"%s\"\n", buf2);

		if (access(buf2, F_OK) == 0) {
			puts(buf2);
			dat.vendor_id = _get_uint16_file(buf2, js);
			char* cursor = strrchr(buf2, '/');
			if(cursor == NULL){
				return dat;
			}
			*cursor = 0;
			strcat(buf2, "/idProduct");
			dat.product_id = _get_uint16_file(buf2, js);
			
			printf("vendor_data = %u:%u\n", dat.vendor_id, dat.product_id);;
			return dat;
		}

		char* cursor = strrchr(buf2, '/');
		if(cursor == NULL){
			return dat;
		}
		*cursor = 0;

		//Check if this is a bluetooth device
		strncat(buf2, "/hidraw", PATH_MAX - len - 1);
		printf("Checking \"%s\"\n", buf2);
		if (access(buf2, F_OK) == 0) {			
			const bool show_hidden = false;
			printf("Attempting to list dir \"%s\"\n", buf2);
			DIR * d = opendir(buf2);
			struct dirent* dir;
			while((dir = readdir(d)) != NULL){
				//Get rid of "." and ".." directory entries
				if(dir->d_name[0] == '.'){
					if(!show_hidden)
						continue;
					if(strlen(dir->d_name) == 1)
						continue;
					if(dir->d_name[1] == '.'){
						if(strlen(dir->d_name) == 2)
						continue;
					}
				}

				bool is_dir = false;
				bool is_file = false;
				struct stat sbuff;
				lstat(dir->d_name, &sbuff);
				if(dir->d_type == DT_UNKNOWN){
					is_dir = S_ISDIR(sbuff.st_mode) && !S_ISLNK(sbuff.st_mode);
				}else{
					is_dir = dir->d_type == DT_DIR;
				}

				if(!is_dir){
					printf("\"%s\" is not DIR!\n", dir->d_name);
					continue;
				}


				if(strncmp(dir->d_name, "hidraw", strlen("hidraw")) == 0){
					sprintf(buf2, "/dev/%s", dir->d_name);
				}else{
					printf("\"%s\" did not match\n", dir->d_name);
					continue;
				}


				if (access(buf2, F_OK) != 0){
					DERROR("Could not access \"%s\" ", buf2);
					return dat;
				}
				int fd = open(buf2, O_RDONLY);
				if(fd < 0){
					DPERROR("Could not get descriptor size from fd %i for path \"%s\" ", fd, buf2);
					return dat;
				}

				int descriptor_size = 0;
				int res = ioctl(fd, HIDIOCGRDESCSIZE, &descriptor_size);
				if(res < 0){
					DPERROR("Could not get descriptor size from fd %i for path \"%s\" ", fd, buf2);
					smart_close(fd);
					return dat;
				}

				//https://docs.huihoo.com/doxygen/linux/kernel/3.7/hid-example_8c_source.html
				/* Get Raw Info */
				struct hidraw_devinfo info;
				res = ioctl(fd, HIDIOCGRAWINFO, &info);
				if (res < 0) {
				    perror("HIDIOCGRAWINFO");
				} else {
					struct hidraw_devinfo info;
				    printf("Raw Info:\n");
				    printf("\tvendor: 0x%04hx\n", info.vendor);
				    printf("\tproduct: 0x%04hx\n", info.product);

					dat.vendor_id = info.vendor;
					dat.product_id = info.product;
					smart_close(fd);
					closedir(d);
					return dat;
				}


				smart_close(fd);
			}

			closedir(d);
		}



		cursor = strrchr(buf2, '/');
		if(cursor == NULL){
			return dat;
		}
		*cursor = 0;
		cursor = strrchr(buf2, '/');
		if(cursor == NULL){
			return dat;
		}
		*cursor = 0;
	}


	#else
	






	#if GET_VENDOR_INFO_FROM_HW_DEVICE
	char* device_path_buffer = (char*)xmalloc(PATH_MAX);
	vendor_data_t dat;
	dat.vendor_id = 0;
	dat.product_id = 0;
	if(js > 99){
		free(device_path_buffer);
		return dat;
	}

	snprintf(device_path_buffer, PATH_MAX, "/sys/class/input/js%i/uevent", js);
	FILE* f = fopen(device_path_buffer, "rb");
	if(f == NULL){
		free(device_path_buffer);
		DPERROR("Could not open \"%s\" for reading! ", device_path_buffer);
		return dat;
	}

	if(fseek(f, 0, SEEK_END) != 0){
		DPERROR("Could not get size of file \"%s\" ", device_path_buffer);
		free(device_path_buffer);
		return dat;
	}
	size_t fsize = ftell(f);

	if(fseek(f, 0, SEEK_SET) != 0){
		DPERROR("Could not move cursor back to start of file for \"%s\" ", device_path_buffer);
		free(device_path_buffer);
		return dat;
	}

	char* file_contents = (char*)xmalloc(fsize + 1);
	file_contents[fsize] = '\0'; //Treat as string. NULL terminate to convert to string
	size_t bytes_read = fread(file_contents, 1, fsize, f);
	if(bytes_read != fsize){
		DERROR("Could not read \"%s\" ", device_path_buffer);
		free(device_path_buffer);
		free(file_contents);
		return dat;
	}

	char* cursor = file_contents;





	free(file_contents);
	free(device_path_buffer);
	return dat;

	#endif

	#if 0
	
	char* device_path_buffer = (char*)alloca(PATH_MAX);
	vendor_data_t dat;
	dat.vendor_id = 0;
	dat.product_id = 0;
	if(js > 99){
		return dat;
	}
	
	sprintf(device_path_buffer, "/sys/class/input/js%u", js);
	
	char buf[PATH_MAX];
	ssize_t len = readlink(device_path_buffer, buf, sizeof(buf));
	if(len <= 0){
		return dat;
	}
	if (len != -1) {
		buf[len] = '\0';
	}

	strcat(device_path_buffer, "/device");

	char buf2[PATH_MAX + strlen("/sys/class/input") + strlen("/idProduct")];
	buf2[0] = 0;
	buf2[PATH_MAX-1] =0;
	len = snprintf(buf2, PATH_MAX, "%s/%s", "/sys/class/input", buf);

	while(true){
		strncat(buf2, "/idVendor", PATH_MAX - len - 1);

		if (access(buf2, F_OK) == 0) {
			dat.vendor_id = _get_uint16_file(buf2, js);
			char* cursor = strrchr(buf2, '/');
			if(cursor == NULL){
				return dat;
			}
			*cursor = 0;
			strcat(buf2, "/idProduct");
			dat.product_id = _get_uint16_file(buf2, js);
			

			return dat;
		}

		char* cursor = strrchr(buf2, '/');
		if(cursor == NULL){
			return dat;
		}
		*cursor = 0;
		cursor = strrchr(buf2, '/');
		if(cursor == NULL){
			return dat;
		}
		*cursor = 0;
	}

	#else




	char* device_path_buffer = (char*)alloca(PATH_MAX);
	vendor_data_t dat;
	dat.vendor_id = 0;
	dat.product_id = 0;
	if(js > 99){
		DERROR("Ran out of joystick slots");
		return dat;
	}

	const char* protocol_str = _get_protocol_string(protocol);

	sprintf(device_path_buffer, "/sys/class/input/%s%u/device/id/vendor", protocol_str, js);
	dat.vendor_id = _get_uint16_file(device_path_buffer, js);
	sprintf(device_path_buffer, "/sys/class/input/%s%u/device/id/product", protocol_str, js);
	dat.product_id = _get_uint16_file(device_path_buffer, js);

	return dat;



	#endif
	#endif
}

static GPAD_CON_MODEL_NINTENDO_T _get_nintendo_product(const vendor_data_t* vend, const char* name){
	const char* MODEL_NAMES[] = {
		NULL,
		NULL,
		"Nintendo Wii Remote Pro Controller",
		"Nintendo Wii Remote",
		NULL //Not sure what the device name is supposed to be. I own 3rd party controller. My 3rd party Switch pro controller reads "Pro Controller". 
	};
	const size_t MODELS_LIST_SIZE = sizeof(MODEL_NAMES)/sizeof(const char*);

	if(name == NULL){
		return GPAD_CON_MODEL_NINTENDO_INVALID;
	}

	switch(vend->product_id){
		case 0x330:
			//Allow code to fall down to name search
		break;
		case 0x2006:
			return GPAD_CON_MODEL_NINTENDO_SWITCH_PRO;
		break;
		default:
			return GPAD_CON_MODEL_NINTENDO_UNKNOWN;
		break;
	}


	for(size_t i = 0; i < MODELS_LIST_SIZE; i++){
		if(MODEL_NAMES[i] == NULL){
			continue;
		}
		if(strncmp(MODEL_NAMES[i], name, NAME_MAX) == 0){
			return (GPAD_CON_MODEL_NINTENDO_T)i;
		}
	}
	return GPAD_CON_MODEL_NINTENDO_UNKNOWN;
}

static GPAD_CON_MODEL_SONY_T _get_sony_product(const vendor_data_t* vend){
	switch(vend->product_id){
		case 0xCE6:
			return GPAD_CON_MODEL_SONY_PS5;
		break;
		default:
			return GPAD_CON_MODEL_SONY_UNKNOWN;
		break;
	}
}

static GPAD_CON_MODEL_XBOX_T _get_xbox_product(const vendor_data_t* vend){
	switch (vend->product_id) {
		case 0x28E:
			return GPAD_CON_MODEL_XBOX_360;
		break;
		default:
			return GPAD_CON_MODEL_XBOX_UNKNOWN;
		break;
	}
}


static inline GPAD_PROTOCOL_MODE_T _get_protocol_mode(const gpad_t* gpad){
	return GPAD_PROTOCOL_MODE_EVDEV;
	switch(gpad->brand){
		case GPAD_CON_NINTENDO:
			switch(gpad->model.nintendo){
				case GPAD_CON_MODEL_NINTENDO_WII_MOTE:
					return GPAD_PROTOCOL_MODE_EVDEV;
				break;
				case GPAD_CON_MODEL_NINTENDO_WII_U_PRO_CONTROLLER:
					return GPAD_PROTOCOL_MODE_JOYDEV;
				break;
				default:
					DERROR("Invalid controller nintendo model %u", gpad->model.nintendo);
					abort();
				break;
			};
		break;
		case GPAD_CON_SONY:
			switch(gpad->model.sony){
				case GPAD_CON_MODEL_SONY_PS5:
					return GPAD_PROTOCOL_MODE_JOYDEV;
				break;
				default:
					DERROR("Invalid controller sony model %u", gpad->model.nintendo);
					abort();
				break;
			};
		break;
		case GPAD_CON_XBOX:
			switch(gpad->model.xbox){
				case GPAD_CON_MODEL_XBOX_360:
					return GPAD_PROTOCOL_MODE_JOYDEV;
				break;
				default:
					DERROR("Invalid controller xbox model %u", gpad->model.nintendo);
					abort();
				break;
			};
		break;
		default:
			DERROR("No valid protocol!");
			return GPAD_PROTOCOL_MODE_INVALID;
		break;
	}

	DERROR("Could not find a valid protocol");
	return GPAD_PROTOCOL_MODE_INVALID;
}

typedef struct{
	size_t button_count;
	size_t axis_count;
	bool is_controller;
}evdev_device_capiblites_t;

#define EVDEV_DEVICE_CAP_INITIALIZER {.button_count = 0, .axis_count=0, .is_controller=false}


static bool  _get_evdev_device_capiblities(int fd, js_t device_id, evdev_device_capiblites_t* out){
	unsigned long evdev_capabilities[EV_MAX / (8 * sizeof(unsigned long)) + 10];
	unsigned long axis_capabilities[ABS_MAX / (8 * sizeof(unsigned long)) + 10];
	unsigned long key_capabilities[KEY_MAX / (8 * sizeof(unsigned long)) + 10];
	
	evdev_device_capiblites_t cap = EVDEV_DEVICE_CAP_INITIALIZER;
	
	if (ioctl(fd, EVIOCGBIT(0, EV_MAX), evdev_capabilities) < 0) {
		DPERROR("Could not get device capabilities for /dev/input/event%i ", device_id);
		return false;
	}
	//Test if device has any axises
	if(test_bit(EV_ABS, evdev_capabilities)){
		if (ioctl(fd, EVIOCGBIT(EV_ABS, ABS_MAX), axis_capabilities) < 0) {
			DPERROR("Could not get stick capabilities for /dev/input/event%i ", device_id);
			return false;
		}

		cap.axis_count = 0;
		for(size_t i = 0; i < ABS_MAX; i++){
			cap.axis_count += test_bit(i, axis_capabilities) != 0;
		}
	}else{
		cap.axis_count = 0;
	}

	//Test for buttons
	if(test_bit(EV_KEY, evdev_capabilities)){
		if (ioctl(fd, EVIOCGBIT(EV_KEY, KEY_MAX), key_capabilities) < 0) {
			DPERROR("Could not get key capabilities for /dev/input/event%i ", device_id);
			return false;
		}

		for(size_t i = 0; i < KEY_MAX; i++){
			cap.button_count += test_bit(i, key_capabilities) != 0;
		}


		static const int COMMON_CONTROLLER_BUTTONS[] = {
			BTN_SOUTH,
			BTN_NORTH,
			BTN_WEST,
			BTN_EAST,
			BTN_START,
			BTN_SELECT,
		};
		for(int i = 0; i < sizeof(COMMON_CONTROLLER_BUTTONS)/sizeof(int); i++){
			cap.is_controller |= (test_bit(COMMON_CONTROLLER_BUTTONS[i], key_capabilities) != 0);
		}


	}else{
		cap.button_count = 0;
	}



	*out = cap;

	return true;
}



#define NAME_SIZE 512
bool gpad_t_construct(gpad_t* gpad, js_t device_id, GPAD_PROTOCOL_MODE_T protocol_mode){
	char* device_path_buffer = (char*)alloca(PATH_MAX);
	gpad->axis = NULL;
	gpad->fd=-1;
	gpad->name=NULL;
	gpad->rumble_event = -1;

	if (device_id > 99){
		return false;
	}


	//Search for events
	#define GPAD_MAX_ALLOWED_EVENTS (20)
	event_t* event_buffer = xmalloc(GPAD_MAX_ALLOWED_EVENTS * sizeof(event_t));
	gpad->events = event_buffer;
	memset(event_buffer, -1, GPAD_MAX_ALLOWED_EVENTS * sizeof(event_t));
	gpad->rumble_event = -1;
	int real_event_len = get_events(device_id, event_buffer, GPAD_MAX_ALLOWED_EVENTS-1, protocol_mode);
	if(real_event_len < 0){
		DERROR("Could not read events associated with controller js=%i", device_id);
		gpad_t_free(gpad);
		return false;
	}
	#if 0
		//Disabled for now
		for(int i = 0; i < real_event_len; i++){
			snprintf(device_path_buffer, PATH_MAX, "/dev/input/event%lli", gpad->events[i]);
			int fd = open(device_path_buffer, O_RDONLY);
			if(fd <= 0){
				DWARN("Could not read event %lli for js/event=%i", gpad->events[i], device_id);
				continue;;
			}
			if(!has_ff_rumble(fd)){
				smart_close(fd);
				continue;
			}
			smart_close(fd);
			fd = -1;

			gpad->rumble_event = gpad->events[i];
			break;
		}
	#else
		gpad->rumble_event = -1;
	#endif

	
	///////////////////////////////

	sprintf(device_path_buffer, "/dev/input/%s%u", _get_protocol_string(protocol_mode),  device_id);



	if(gpad->fd <= 0){
		gpad->fd = open(device_path_buffer, O_NONBLOCK | O_RDONLY);
		if(gpad->fd <= 0){
			gpad_t_free(gpad);
			DPERROR("Could not open \"%s\" for reading. ", device_path_buffer);
			return false;
		}
	}else{
		DERROR("Could not open \"%s\" for reading. The FD for this gamepad is already initalized!", device_path_buffer);
		gpad_t_free(gpad);
		return false;
	}

	//Get name
	switch(protocol_mode){
		case GPAD_PROTOCOL_MODE_JOYDEV:
			gpad->name = (char*)xmalloc(NAME_SIZE);
			if (ioctl(gpad->fd, JSIOCGNAME(NAME_SIZE), gpad->name) < 0)
				strncpy(gpad->name, "Unknown", NAME_SIZE);
		break;
		case GPAD_PROTOCOL_MODE_EVDEV:
			gpad->name = (char*)xmalloc(NAME_SIZE);
			if (ioctl(gpad->fd, EVIOCGNAME(NAME_SIZE), gpad->name) < 0)
				strncpy(gpad->name, "Unknown", NAME_SIZE);
		break;
		default:
			DERROR("Unknown protocol %i!", (int)protocol_mode);
			gpad_t_free(gpad);
			return false;
		break;
	};


	gpad->name[NAME_SIZE-1]=0;

	vendor_data_t vendor = _get_vendor_data(device_id, protocol_mode);
	GPAD_CON_MODEL_T model;
	model.sony = GPAD_CON_MODEL_SONY_INVALID;
	switch(vendor.vendor_id){ 
		case 0x054c://Sony
			gpad->brand = GPAD_CON_SONY;
			gpad->model.sony = _get_sony_product(&vendor);
			DINFO("SONY %X:%X\n", vendor.vendor_id, vendor.product_id);
		break;
		case 0x045E:
			gpad->brand = GPAD_CON_XBOX;
			gpad->model.xbox = _get_xbox_product(&vendor);
			DINFO("MICROSOFT XBOX %X:%X\n", vendor.vendor_id, vendor.product_id);
		break;
		case 0x057E: //Nintendo
			gpad->brand = GPAD_CON_NINTENDO;
			gpad->model.nintendo = _get_nintendo_product(&vendor, gpad->name);
			DINFO("NINTENDO %X:%X_%X\n", vendor.vendor_id, vendor.product_id, gpad->model.nintendo);
		break;
		case 0:
			gpad->brand = GPAD_CON_INVALID;
			model.sony = GPAD_CON_MODEL_SONY_INVALID;
			gpad->model = model;
		break;
		default:
			gpad->brand = GPAD_CON_UNKNOWN;
			gpad->model.sony = GPAD_CON_MODEL_SONY_UNKNOWN;
			DINFO("UNKNOWN %X:%X\n", vendor.vendor_id, vendor.product_id);
		break;
	}

	gpad->connection_mode = _get_protocol_mode(gpad);

	char number_of_axes = -1;
	char number_of_buttons = -1;
	int ret_val = -1;
	switch(gpad->connection_mode){
		case GPAD_PROTOCOL_MODE_JOYDEV:
			//Get number of axises
			ret_val = ioctl (gpad->fd, JSIOCGAXES, &number_of_axes);

			if(number_of_axes <= 0 || ret_val < 0){
				DWARN("Number of sticks are less than 0! Assuming this is a non-analog controller");
				number_of_axes = 0;
			}

			if(number_of_axes > 0){
				gpad->axis_count = number_of_axes;
				gpad->axis = (gpad_axis_t*)xmalloc(sizeof(gpad_axis_t) * gpad->axis_count);
				for(size_t i = 0; i < gpad->axis_count; i++){
					gpad->axis[i].x = 0;
					gpad->axis[i].y = 0;
				}
			}else{
				gpad->axis_count = 0;
				gpad->axis = NULL;
			}

			//get button count
			ret_val = ioctl (gpad->fd, JSIOCGBUTTONS, &number_of_buttons);
			if(ret_val < 0){
				DPERROR("Could not ictl for button count. ");
				number_of_buttons = 0;
			}

			if(number_of_buttons <= 0 || ret_val < 0){
				DWARN("Number of buttons are less than 0! Assuming this is an analog-only controller\n");
				number_of_buttons = 0;
			}

			gpad->button_count = number_of_buttons;
			gpad->buttons = 0;

		break;

		case GPAD_PROTOCOL_MODE_EVDEV:
			gpad->axis_count = 0;
			gpad->axis = NULL;
			gpad->button_count = 0;
			gpad->buttons = 0;

			
			evdev_device_capiblites_t cap = EVDEV_DEVICE_CAP_INITIALIZER;

			if(_get_evdev_device_capiblities(gpad->fd, device_id, &cap) == false){
				DERROR("Could not get device cap!");
				gpad_t_free(gpad);
				return false;
			}

			gpad->button_count = cap.button_count;
			gpad->axis_count = cap.axis_count;

			if(gpad->button_count <= 0){
				DWARN("Number of buttons are less than 0! Assuming this is an analog-only controller\n");
			}
			if(gpad->axis_count <= 0){
				DWARN("Number of sticks are less than 0! Assuming this is a non-analog controller");
			}else{
				gpad->axis = (gpad_axis_t*)xmalloc(sizeof(gpad_axis_t) * gpad->axis_count);
				for(size_t i = 0; i < gpad->axis_count; i++){
					gpad->axis[i].x = 0;
					gpad->axis[i].y = 0;
				}
			}


		break;
		default:
			DERROR("Unknown protocol %u", gpad->connection_mode);
			gpad_t_free(gpad);
			return false;
		break;
	}



	return true;
}


typedef struct{
	int event_id;
    int js;
	vendor_data_t vend;
    int vers;
	GPAD_PROTOCOL_MODE_T protocol;
}os_blob_real_t;

typedef struct{
	char* name;
	os_blob_real_t data;
}gpad_device_list_ent_real_t;


    

typedef struct{
	const char* name;
	size_t str_len;
}soft_blacklist_entry_t;
#define DEFINE_SOFT_BLACKLIST_ENTRY(str) {.name=str, .str_len=strlen(str)}

static soft_blacklist_entry_t SOFT_BLACKLISTED_DEVICES_FOR_LISTING[] = {
	DEFINE_SOFT_BLACKLIST_ENTRY("Sony Interactive Entertainment DualSense Wireless Controller Motion Sensors"),
	DEFINE_SOFT_BLACKLIST_ENTRY("DualSense Wireless Controller Motion Sensors")
};
#define SOFT_BLACKLISTED_DEVICES_FOR_LISTING_SIZE (sizeof(SOFT_BLACKLISTED_DEVICES_FOR_LISTING)/sizeof(soft_blacklist_entry_t))

static inline bool is_device_list_soft_blacklisted(const char* name){
	for(size_t blist_index = 0; blist_index < SOFT_BLACKLISTED_DEVICES_FOR_LISTING_SIZE; blist_index++){
		if(strncmp(SOFT_BLACKLISTED_DEVICES_FOR_LISTING[blist_index].name, name, SOFT_BLACKLISTED_DEVICES_FOR_LISTING[blist_index].str_len) == 0){
			return true;
		}
	}
	return false;
}

/*
	Lists all of the devices. This allocates an array of arrays. Use 'gpad_device_list_free' to free.
	Returns NULL on failure or no devices
*/
gpad_device_list_t gpad_list_evdev_devices(void){
	#if 0
	size_t num_of_devices = 0;
	char* string_mem = (char*)xmalloc(PATH_MAX * 3); //Optimization to try to get larger malloc block and more cache performance
	char* device_path_buffer = string_mem + (PATH_MAX * 0);
	char* link_buffer = string_mem + (PATH_MAX * 1);
	char* realpath_buffer = string_mem + (PATH_MAX * 2);

	DIR * d = opendir("/dev/input/by-id");
	struct dirent* dir;

	if(d == NULL){
		if(errno == ENOENT){
			return NULL;
		}
		DPERROR("Could not list the input directory!");
		return NULL;
	}

	#define MAX_ALLOWED_GPADS 100
	size_t connected_devices[MAX_ALLOWED_GPADS];
	size_t element_count = 0;
	memset(connected_devices, 0, sizeof(connected_devices));

	const bool show_hidden = false;

	while((dir = readdir(d)) != NULL){
		//Get rid of "." and ".." directory entries
		if(dir->d_name[0] == '.'){
			if(!show_hidden)
				continue;
			if(strlen(dir->d_name) == 1)
				continue;
			if(dir->d_name[1] == '.'){
				if(strlen(dir->d_name) == 2)
				continue;
			}
		}

		bool is_dir = false;
		bool is_file = false;
		struct stat sbuff;
		lstat(dir->d_name, &sbuff);
		if(dir->d_type == DT_UNKNOWN){
			is_dir = S_ISDIR(sbuff.st_mode) && !S_ISLNK(sbuff.st_mode);
  		}else{
			is_dir = dir->d_type == DT_DIR;
		}

		if(is_dir){
			continue;
		}


		size_t element_strlen = strnlen(dir->d_name, PATH_MAX);
		if(element_strlen <= 2){
			continue;
		}
		//Ensure NULL terminated
		dir->d_name[element_strlen] = '\0';
		if(_string_ends_with(dir->d_name, "-event-joystick") == false){
			continue;
		}

		snprintf(device_path_buffer, PATH_MAX, "/dev/input/by-id/%s", dir->d_name);
		ssize_t len = readlink(device_path_buffer, link_buffer, PATH_MAX);
		if(len <= 0){
			DPERROR("Could not read link for \"%s\" ", device_path_buffer);
			continue;
		}
		//Ensure null term
		if (len != -1) {
			link_buffer[len] = '\0';
			link_buffer[PATH_MAX-1] = '\0';
		}

		char* last_slash_character = strrchr(link_buffer, '/');
		if(last_slash_character == NULL){
			DERROR("Controller \"%s\" was symlinked to \"%s\" but did not contain any slashes!", device_path_buffer, link_buffer);
			continue;
		}
		char* start_of_event_string = last_slash_character + 1;

		size_t event_name_strlen = strnlen(last_slash_character, PATH_MAX);
		if(event_name_strlen < strlen("event0")){
			DERROR("Controller \"%s\" was determined to be linked to \"%s\" which is too small of a string to match to a valid event[XYZ]!", device_path_buffer, link_buffer);
			continue;
		}


		const char* start_of_int_ptr = start_of_event_string + strlen("event");
		if(sscanf(start_of_int_ptr, "%lu", &connected_devices[element_count]) <= 0){
			DPERROR("Could not sscanf an integer from path %s  ", link_buffer);
			continue;
		}

		element_count++;
		if(element_count >= MAX_ALLOWED_GPADS){
			break;
		}
	}

	closedir(d);

	num_of_devices = element_count;

	gpad_device_list_t ret = (gpad_device_list_t)xmalloc(sizeof(gpad_device_list_ent_real_t*) * (num_of_devices + 1));
	
	size_t cur_cursor = 0;
	for(size_t i = 0; i < num_of_devices; i++){
		char* name = (char*)xmalloc(NAME_SIZE);
		memset(name, 0, NAME_SIZE);

		size_t event = connected_devices[i];

		sprintf(device_path_buffer, "/dev/input/event%lu", event);
		int fd = open(device_path_buffer, O_NONBLOCK | O_RDONLY);
		if (ioctl(fd, EVIOCGNAME(NAME_SIZE), name) < 0)
			strncpy(name, "Unknown", NAME_SIZE);
		smart_close(fd);
		
		//Check if we dont want to list this device
		if(is_device_list_soft_blacklisted(name)){
			free(name);
			ret[cur_cursor] = NULL;
			continue;
		}

		gpad_device_list_ent_real_t* dev_ent = (gpad_device_list_ent_real_t*)xmalloc(sizeof(gpad_device_list_ent_real_t));
		dev_ent->name = name;
		dev_ent->data.js = -1;
		vendor_data_t vend = _get_vendor_data(event, GPAD_PROTOCOL_MODE_EVDEV);
		dev_ent->data.vend = vend;
		dev_ent->data.vers = -1;
		dev_ent->data.event_id = event;
		dev_ent->data.protocol = GPAD_PROTOCOL_MODE_EVDEV;


		ret[cur_cursor] = (gpad_device_list_ent_t*)dev_ent;
		cur_cursor++;
		if(name == NULL){
			free(string_mem);
			return ret;
		}
	}
	ret[num_of_devices] = NULL;
	free(string_mem);
	return ret;

	#else

	size_t num_of_devices = 0;
	char* string_mem = (char*)xmalloc(PATH_MAX * 3); //Optimization to try to get larger malloc block and more cache performance
	char* device_path_buffer = string_mem + (PATH_MAX * 0);
	char* link_buffer = string_mem + (PATH_MAX * 1);
	char* realpath_buffer = string_mem + (PATH_MAX * 2);

	DIR * d = opendir("/sys/class/input");
	struct dirent* dir;

	if(d == NULL){
		if(errno == ENOENT){
			return NULL;
		}
		DPERROR("Could not list the input directory!");
		return NULL;
	}

	#define MAX_ALLOWED_GPADS 100
	size_t connected_devices[MAX_ALLOWED_GPADS];
	size_t element_count = 0;
	memset(connected_devices, 0, sizeof(connected_devices));

	const bool show_hidden = false;

	while((dir = readdir(d)) != NULL){
		//Get rid of "." and ".." directory entries
		if(dir->d_name[0] == '.'){
			if(!show_hidden)
				continue;
			if(strlen(dir->d_name) == 1)
				continue;
			if(dir->d_name[1] == '.'){
				if(strlen(dir->d_name) == 2)
				continue;
			}
		}

		bool is_dir = false;
		bool is_file = false;
		struct stat sbuff;
		lstat(dir->d_name, &sbuff);
		if(dir->d_type == DT_UNKNOWN){
			is_dir = S_ISDIR(sbuff.st_mode) && !S_ISLNK(sbuff.st_mode);
  		}else{
			is_dir = dir->d_type == DT_DIR;
		}

		if(is_dir){
			continue;
		}


		size_t element_strlen = strnlen(dir->d_name, PATH_MAX);
		if(element_strlen <= 2){
			continue;
		}
		//Ensure NULL terminated
		dir->d_name[element_strlen] = '\0';

		size_t event_name_strlen = strnlen(dir->d_name, PATH_MAX);
		if(event_name_strlen < strlen("event0")){
			continue;
		}

		if(strncmp(dir->d_name, "event", strlen("event")) != 0){
			continue;
		}


		const char* start_of_int_ptr = dir->d_name + strlen("event");
		size_t event_num = 0;
		if(sscanf(start_of_int_ptr, "%lu", &event_num) <= 0){
			DPERROR("Could not sscanf an integer from path %s  ", dir->d_name);
			continue;
		}

		
		sprintf(device_path_buffer, "/dev/input/event%li", event_num);
		int fd = open(device_path_buffer, O_RDONLY | O_NONBLOCK);
		if(fd < 0){
			if(errno == EACCES){
				continue;
			}
			DPERROR("Could not open \"%s\" for reading! ", device_path_buffer);
			continue;
		}
		//TODO: HEre
		
		evdev_device_capiblites_t cap = EVDEV_DEVICE_CAP_INITIALIZER;

		if(_get_evdev_device_capiblities(fd, event_num, &cap) == false){
			continue;
		}
		smart_close(fd);

		if(cap.button_count <= 0){
			continue;
		}
		if(cap.is_controller == false){
			DWARN("Gamepad /dev/intput/event%li has buttons but did not have normal controller buttons! Still treating it as a controller", event_num);
		}

		connected_devices[element_count] = event_num;
		element_count++;
		if(element_count >= MAX_ALLOWED_GPADS){
			break;
		}
	}

	closedir(d);

	num_of_devices = element_count;

	gpad_device_list_t ret = (gpad_device_list_t)xmalloc(sizeof(gpad_device_list_ent_real_t*) * (num_of_devices + 1));
	
	size_t cur_cursor = 0;
	for(size_t i = 0; i < num_of_devices; i++){
		char* name = (char*)xmalloc(NAME_SIZE);
		memset(name, 0, NAME_SIZE);

		size_t event = connected_devices[i];

		sprintf(device_path_buffer, "/dev/input/event%lu", event);
		int fd = open(device_path_buffer, O_NONBLOCK | O_RDONLY);
		if (ioctl(fd, EVIOCGNAME(NAME_SIZE), name) < 0)
			strncpy(name, "Unknown", NAME_SIZE);
		smart_close(fd);
		
		//Check if we dont want to list this device
		if(is_device_list_soft_blacklisted(name)){
			free(name);
			ret[cur_cursor] = NULL;
			continue;
		}

		gpad_device_list_ent_real_t* dev_ent = (gpad_device_list_ent_real_t*)xmalloc(sizeof(gpad_device_list_ent_real_t));
		dev_ent->name = name;
		dev_ent->data.js = -1;
		vendor_data_t vend = _get_vendor_data(event, GPAD_PROTOCOL_MODE_EVDEV);
		dev_ent->data.vend = vend;
		dev_ent->data.vers = -1;
		dev_ent->data.event_id = event;
		dev_ent->data.protocol = GPAD_PROTOCOL_MODE_EVDEV;


		ret[cur_cursor] = (gpad_device_list_ent_t*)dev_ent;
		cur_cursor++;
		if(name == NULL){
			free(string_mem);
			return ret;
		}
	}
	ret[cur_cursor] = NULL;
	free(string_mem);
	return ret;



	#endif
}



/*
	Lists all of the devices. This allocates an array of arrays. Use 'gpad_device_list_free' to free.
	Returns NULL on failure or no devices
*/
gpad_device_list_t gpad_list_joydev_devices(void){
	size_t num_of_devices = 0;
	char* device_path_buffer = (char*)alloca(PATH_MAX);

	DIR * d = opendir("/sys/class/input");
	struct dirent* dir;

	if(d == NULL){
		DPERROR("Could not list the input directory!");
		return NULL;
	}

	#define MAX_ALLOWED_GPADS 100
	size_t connected_devices[MAX_ALLOWED_GPADS];
	size_t element_count = 0;
	memset(connected_devices, 0, sizeof(connected_devices));

	const bool show_hidden = false;

	while((dir = readdir(d)) != NULL){
		//Get rid of "." and ".." directory entries
		if(dir->d_name[0] == '.'){
			if(!show_hidden)
				continue;
			if(strlen(dir->d_name) == 1)
				continue;
			if(dir->d_name[1] == '.'){
				if(strlen(dir->d_name) == 2)
				continue;
			}
		}

		bool is_dir = false;
		bool is_file = false;
		struct stat sbuff;
		lstat(dir->d_name, &sbuff);
		if(dir->d_type == DT_UNKNOWN){
			is_dir = S_ISDIR(sbuff.st_mode) && !S_ISLNK(sbuff.st_mode);
  		}else{
			is_dir = dir->d_type == DT_DIR;
		}

		if(is_dir){
			continue;
		}


		size_t element_strlen = strnlen(dir->d_name, sizeof(dir->d_name));
		if(element_strlen <= 2){
			continue;
		}
		//Ensure NULL terminated
		dir->d_name[element_strlen] = '\0';
		if(dir->d_name[0] != 'j' || dir->d_name[1] != 's'){
			continue;
		}

		const char* start_of_int_ptr = dir->d_name + 2;
		if(sscanf(start_of_int_ptr, "%lu", &connected_devices[element_count]) <= 0){
			DPERROR("Could not sscanf an integer from path /sys/class/input/%s  ", dir->d_name);
			continue;
		}

		element_count++;
		if(element_count >= MAX_ALLOWED_GPADS){
			break;
		}
	}

	closedir(d);

	num_of_devices = element_count;

	gpad_device_list_t ret = (gpad_device_list_t)xmalloc(sizeof(gpad_device_list_ent_real_t*) * (num_of_devices + 1));
	
	size_t cur_cursor = 0;
	for(size_t i = 0; i < num_of_devices; i++){
		char* name = (char*)xmalloc(NAME_SIZE);
		memset(name, 0, NAME_SIZE);

		size_t js = connected_devices[i];

		sprintf(device_path_buffer, "/dev/input/js%lu", js);
		int fd = open(device_path_buffer, O_NONBLOCK | O_RDONLY);
		if (ioctl(fd, JSIOCGNAME(NAME_SIZE), name) < 0)
			strncpy(name, "Unknown", NAME_SIZE);
		smart_close(fd);
		
		//Check if we dont want to list this device
		if(is_device_list_soft_blacklisted(name)){
			free(name);
			ret[cur_cursor] = NULL;
			continue;
		}

		gpad_device_list_ent_real_t* dev_ent = (gpad_device_list_ent_real_t*)xmalloc(sizeof(gpad_device_list_ent_real_t));
		dev_ent->name = name;
		dev_ent->data.js = js;
		vendor_data_t vend = _get_vendor_data(js, GPAD_PROTOCOL_MODE_JOYDEV);
		dev_ent->data.vend = vend;
		dev_ent->data.vers = -1;
		dev_ent->data.event_id = -1;
		dev_ent->data.protocol = GPAD_PROTOCOL_MODE_JOYDEV;


		ret[cur_cursor] = (gpad_device_list_ent_t*)dev_ent;
		cur_cursor++;
		if(name == NULL){
			return ret;
		}
	}
	ret[cur_cursor] = NULL;
	return ret;
}


gpad_device_list_t gpad_list_devices(void){
	return gpad_list_evdev_devices();
}

gpad_device_list_ent_t* gpad_device_list_get(gpad_device_list_t dev_list, unsigned int index){
	gpad_device_list_ent_t** cursor = (gpad_device_list_ent_t**)dev_list;
	size_t i = 0;
	while(*cursor != NULL){
		if(i != index){
			i++;
			cursor++;
			continue;
		}
		return *cursor;
	}
	return NULL;
}

gpad_device_list_ent_t* gpad_device_list_ent_memdup(const gpad_device_list_ent_t* ent){
	gpad_device_list_ent_t* ret = (gpad_device_list_ent_t*)xmalloc(sizeof(gpad_device_list_ent_real_t));
	memcpy(ret, ent, sizeof(gpad_device_list_ent_real_t));
	ret->name = strndup(ent->name, PATH_MAX);
	SMART_ASSERT(ret->name != NULL, "Could not dup ent name!");
	return ret;
}

void gpad_device_list_ent_real_free(gpad_device_list_ent_real_t* obj){
	free(obj->name);
	free(obj);
}

void gpad_device_list_free(gpad_device_list_t device_list){
	gpad_device_list_ent_t** cursor = (gpad_device_list_ent_t**)device_list;
	if(device_list == NULL){
		return;
	}
	while(*cursor != NULL){
		gpad_device_list_ent_real_free((gpad_device_list_ent_real_t*)*cursor);
		cursor++;
	}
	free(device_list);
}


bool gpad_construct_from_device_list_ent(gpad_t* gpad, const gpad_device_list_ent_t* ent){
	gpad_device_list_ent_real_t* real_ent = (gpad_device_list_ent_real_t*)ent;

	js_t id = -1;
	switch(real_ent->data.protocol){
		case GPAD_PROTOCOL_MODE_JOYDEV:
			id = real_ent->data.js;
		break;
		case GPAD_PROTOCOL_MODE_EVDEV:
			id = real_ent->data.event_id;
		break;
		default:
			DERROR("Unsupported protocol %i!", (int)real_ent->data.protocol);
		break;
	}

	return gpad_t_construct(gpad, id, real_ent->data.protocol);
}