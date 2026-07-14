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

#include <pthread.h>
#include <fcntl.h>
#include <errno.h>


#include <dirent.h>



int gpad_read(gpad_t* gpad){
	controller_event e;

	if(gpad->fd < 0){
		return READ_DEAD_CONTROLLER;
	}

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
		close(gpad->fd);
		gpad->fd = -1;
		return READ_FAILED;
	}

	return READ_OK;
}


typedef struct{
	u_int16_t vendor_id;
	u_int16_t product_id;
}vendor_data_t;


static u_int16_t _get_uint16_file(const char file_path[], int js){
	const char* buf2 = file_path;
	FILE* f = fopen(buf2, "r");
	if(f == NULL){
		DPERROR("Could not open VENDOR ID for controller %i  ", js);
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


static int get_events(int js, int events[], int events_len){
	if(events_len <= 0){
		return -1;
	}
	int real_event_len = 0;
	char* path = (char*)alloca(PATH_MAX);
	path[0] = 0;
	path[PATH_MAX-1] = 0;
	int a = snprintf(path, PATH_MAX, "/sys/class/input/js%u/device", js);
	if(a <= strlen("/sys/class/input/js")){
		return -1;
	}
	
	DIR *dir = opendir(path);
    if (!dir) {
        DPERROR("Could not get events for js=%i", js);
        return -1;
    }

    struct dirent *entry;

    // Look for eventX inside js0 device directory
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
			a = sscanf(entry->d_name + strlen("event"), "%u", &events[real_event_len++]);
			if(a < 1){
				return -1;
			}
			if(real_event_len >= events_len){
				return real_event_len;
			}

            //break;
        }
    }

    closedir(dir);

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

static int has_ff_rumble(int fd)
{
    unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];
    unsigned long ff_bits[BITS_TO_LONGS(FF_MAX)];

    memset(ev_bits, 0, sizeof(ev_bits));
    memset(ff_bits, 0, sizeof(ff_bits));

    /* First: does the device support force feedback at all? */
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0)
        return 0;

    if (!test_bit(EV_FF, ev_bits))
        return 0;

    /* Now query which FF effects are supported */
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) < 0)
        return 0;

    return test_bit(FF_RUMBLE, ff_bits);
}


static vendor_data_t _get_vendor_data(int js){

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
					close(fd);
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
					close(fd);
					closedir(d);
					return dat;
				}


				close(fd);
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
		return dat;
	}
	
	sprintf(device_path_buffer, "/sys/class/input/js%u/device/id/vendor", js);
	dat.vendor_id = _get_uint16_file(device_path_buffer, js);
	sprintf(device_path_buffer, "/sys/class/input/js%u/device/id/product", js);
	dat.product_id = _get_uint16_file(device_path_buffer, js);

	return dat;



	#endif
	#endif
}

static GPAD_CON_MODEL_NINTENDO_T _get_nintendo_product(const char* name){
	const char* MODEL_NAMES[] = {
		NULL,
		NULL,
		"Nintendo Wii Remote Pro Controller",
		"Nintendo Wii Remote",
		NULL
	};
	const size_t MODELS_LIST_SIZE = sizeof(MODEL_NAMES)/sizeof(const char*);

	if(name == NULL){
		return GPAD_CON_MODEL_NINTENDO_INVALID;
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



#define NAME_SIZE 512
bool gpad_t_construct(gpad_t* gpad, unsigned int js){
	char* device_path_buffer = (char*)alloca(PATH_MAX);
	gpad->axis = NULL;
	gpad->fd=-1;
	gpad->name=NULL;
	gpad->rumble_event = -1;

	if (js > 99){
		return false;
	}


	//Search for events
	int events[10];
	int real_event_len = get_events(js, events, sizeof(events)/sizeof(int));
	if(real_event_len < 0){
		DERROR("Could not read events associated with controller js=%i", js);
		return false;
	}
	for(int i = 0; i < real_event_len; i++){
		snprintf(device_path_buffer, PATH_MAX, "/dev/input/event%i", events[i]);
		int fd = open(device_path_buffer, O_RDONLY);
		if(fd <= 0){
			DWARN("Could not read event %i for js=%i", events[i], js);
			continue;;
		}
		if(!has_ff_rumble(fd)){
			close(fd);
			continue;
		}
		close(fd);
		fd = -1;

		gpad->rumble_event = events[i];
		break;
	}

	
	///////////////////////////////

	sprintf(device_path_buffer, "/dev/input/js%u", js);



	if(gpad->fd <= 0){
		gpad->fd = open(device_path_buffer, O_NONBLOCK | O_RDONLY);
		if(gpad->fd <= 0){
			gpad->axis = NULL;
			gpad->fd=-1;
			gpad->name=NULL;
			DPERROR("Could not open \"%s\" for reading. ", device_path_buffer);
			return false;
		}
	}else{
		DERROR("Could not open \"%s\" for reading. The FD for this gamepad is already initalized!", device_path_buffer);
		gpad->axis = NULL;
		gpad->fd=-1;
		gpad->name=NULL;
		return false;
	}

	//Get name
	gpad->name = (char*)xmalloc(NAME_SIZE);
	if (ioctl(gpad->fd, JSIOCGNAME(NAME_SIZE), gpad->name) < 0)
		strncpy(gpad->name, "Unknown", NAME_SIZE);
	gpad->name[NAME_SIZE-1]=0;

	vendor_data_t vendor = _get_vendor_data(js);
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
			gpad->model.nintendo = _get_nintendo_product(gpad->name);
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


	//Get number of axises
	char number_of_axes;
	int ret_val = ioctl (gpad->fd, JSIOCGAXES, &number_of_axes);

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
	char number_of_buttons;
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



	return true;
}


typedef struct{
	int event_id;
    int js;
	vendor_data_t vend;
    int vers;
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
gpad_device_list_t gpad_list_devices(void){
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
		close(fd);
		
		//Check if we dont want to list this device
		if(is_device_list_soft_blacklisted(name)){
			free(name);
			ret[cur_cursor] = NULL;
			continue;
		}

		gpad_device_list_ent_real_t* dev_ent = (gpad_device_list_ent_real_t*)xmalloc(sizeof(gpad_device_list_ent_real_t));
		dev_ent->name = name;
		dev_ent->data.js = js;
		vendor_data_t vend = _get_vendor_data(js);
		dev_ent->data.vend = vend;
		dev_ent->data.vers = -1;
		dev_ent->data.event_id = -1;


		ret[cur_cursor] = (gpad_device_list_ent_t*)dev_ent;
		cur_cursor++;
		if(name == NULL){
			return ret;
		}
	}
	ret[num_of_devices] = NULL;
	return ret;
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
	while(*cursor != NULL){
		gpad_device_list_ent_real_free((gpad_device_list_ent_real_t*)*cursor);
		cursor++;
	}
	free(device_list);
}


bool gpad_construct_from_device_list_ent(gpad_t* gpad, const gpad_device_list_ent_t* ent){
	gpad_device_list_ent_real_t* real_ent = (gpad_device_list_ent_real_t*)ent;
	return gpad_t_construct(gpad, real_ent->data.js);
}

void gpad_t_free(gpad_t* gpad){
	if(gpad->axis != NULL)
		free(gpad->axis);
	if(gpad->name != NULL)
		free(gpad->name);
	gpad->axis = NULL;
	gpad->name = NULL;
	if(gpad->fd >= 0){
		SMART_ASSERT(close(gpad->fd) == 0, "Could not close gamepad");
	}
	memset(gpad, 0, sizeof(gpad_t));
}