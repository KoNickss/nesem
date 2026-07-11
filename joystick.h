#ifndef JOYSTICK_H
#define JOYSTICK_H
#include <stddef.h>
#include <stdbool.h>

typedef enum{
	READ_INVALID,
	READ_OK,
	READ_FAILED,
	READ_DEAD_CONTROLLER
}GPAD_READ_RESULT_T;


typedef union{
    struct{
        float x;
        float y;
    };
    float arrary[2];
}gpad_axis_t;

typedef unsigned long gpad_button_t;


typedef enum{
    GPAD_CON_INVALID,
    GPAD_CON_UNKNOWN,
    GPAD_CON_SONY,
    GPAD_CON_XBOX,
    GPAD_CON_NINTENDO
}GPAD_CON_BRAND_T;

typedef enum{
    GPAD_CON_MODEL_SONY_INVALID,
    GPAD_CON_MODEL_SONY_UNKNOWN,
    GPAD_CON_MODEL_SONY_PS5,
    GPAD_CON_MODEL_SONY_PS4
}GPAD_CON_MODEL_SONY_T;

typedef enum{
    GPAD_CON_MODEL_XBOX_INVALID,
    GPAD_CON_MODEL_XBOX_UNKNOWN,
    GPAD_CON_MODEL_XBOX_360,
}GPAD_CON_MODEL_XBOX_T;


typedef union{
    GPAD_CON_MODEL_SONY_T sony;
    GPAD_CON_MODEL_XBOX_T xbox;
}GPAD_CON_MODEL_T;


typedef struct{
    int fd;
    gpad_axis_t* axis;
    int rumble_event;
    char* name;
    gpad_button_t buttons;
    size_t axis_count;
    size_t button_count;
    GPAD_CON_BRAND_T brand;
    GPAD_CON_MODEL_T model;
}gpad_t;

//void gpad_init();

bool gpad_t_construct(gpad_t* gpad, unsigned int js);

void gpad_t_free(gpad_t* gpad);

int gpad_read(gpad_t* gpad);

gpad_t gpad_thread_read();

void gpad_thread_stop();



typedef void* os_blob;


typedef struct{
    char* name;
    os_blob data;
}gpad_device_list_ent_t;



typedef gpad_device_list_ent_t** gpad_device_list_t;

gpad_device_list_t gpad_list_devices(void);

bool gpad_construct_from_device_list_ent(gpad_t* gpad, const gpad_device_list_ent_t* ent);

void gpad_device_list_free(gpad_device_list_t device_list);

gpad_device_list_ent_t* gpad_device_list_ent_memdup(const gpad_device_list_ent_t* ent);

gpad_device_list_ent_t* gpad_device_list_get(gpad_device_list_t dev_list, unsigned int index);
#endif