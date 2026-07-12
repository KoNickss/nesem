#include "window.h"
#include "controller.h"
#include "joystick.h"
#include "sound.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xcms.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

#define WINDOW_TITLE "nesem"

//Just in case we want to add clickable options on the sidbar
#define WINDOW_DRAW_OFFSET_X (0)
#define WINDOW_DRAW_OFFSET_Y (0)

#define WINDOW_ENABLE_CONTROLLER_OVERLAY true

static Display* dis;
static int screen;
static Window win;
static GC gc;

static win_size_t window_width;
static win_size_t window_height;

static win_size_t window_mouse_x;
static win_size_t window_mouse_y;

static pthread_t window_thread_id;
static pthread_mutex_t mutex;
static void* window_thread(void*);

typedef unsigned char ColorChannel;
typedef unsigned int RGBA;
//Structure to contain color values in the format Blue, Green, Red. This is opposed to the normal RGB
typedef union{
    struct{
        ColorChannel b;
        ColorChannel g;
        ColorChannel r;
        ColorChannel a;
    }BGRA;

    struct{
        ColorChannel r;
        ColorChannel g;
        ColorChannel b;
        ColorChannel a;
    }RGBA;    
    ColorChannel channels[4];
}Color;


//Stores the raw image data that gets displayed to the screen
typedef struct{
    XImage* ximg;
    win_size_t width;
    win_size_t height;
}Image;

static struct {
    Image img_dat;
    Pixmap frame;
}window_framebuffer;
static bool window_framebuffer_updated = false;

#define USE_AGBR 1
#define INDEX_PIXEL(buffer, x, y, width) (&((Color*)buffer)[y * width + x])
static void Image_create(Image* __restrict o, Color* __restrict raw_pixels, win_size_t width, win_size_t height){
    o->width = width;
    o->height = height;

    Color* raw_output = (Color*)xmalloc(sizeof(Color) * width * height);
    if(raw_output == NULL){
        fprintf(stderr, "ERR: Out of memory! Tried to allocate %lu bytes\n", width * height * sizeof(Color));
        abort();
    }

    #if USE_AGBR
        //Convert RGBA to ABGR and copy image
        Color tmp;
        Color* RGBA_cursor = raw_pixels;
        Color* ABGR_cursor = raw_output;
        for(win_size_t i = 0; i < width * height; i++){
            ABGR_cursor->BGRA.r = RGBA_cursor->RGBA.r;
            ABGR_cursor->BGRA.g = RGBA_cursor->RGBA.g;
            ABGR_cursor->BGRA.b = RGBA_cursor->RGBA.b;
            ABGR_cursor->BGRA.a = RGBA_cursor->RGBA.a;

            RGBA_cursor++;
            ABGR_cursor++;
        }
    #else
        memcpy(raw_output, raw_pixels, sizeof(Color) * width * height);
    #endif
   

    o->ximg = XCreateImage(dis, DefaultVisual(dis, 0), 24, ZPixmap, 0, (char*)raw_output, width, height, 32, 0);
}

static void Image_destroy(Image* __restrict o){
    //NOTE: do not worry about freeing 'raw_output' from Image_create. XDestroyImage will free it for you
    XDestroyImage(o->ximg);
    o->ximg = NULL;
}



void window_init(win_size_t width, win_size_t height){
    window_width = width + WINDOW_DRAW_OFFSET_X;
    window_height = height + WINDOW_DRAW_OFFSET_Y;
    window_mouse_x = 0;
    window_mouse_y = 0;

    //Init Display
    dis=XOpenDisplay(NULL);
    screen=DefaultScreen(dis);

    //Create Window
    win=XCreateSimpleWindow(dis,DefaultRootWindow(dis),0,0,	width, height, 5, BlackPixel(dis,screen), WhitePixel(dis, screen));
    XSetStandardProperties(dis,win, WINDOW_TITLE,WINDOW_TITLE,None,NULL,0,NULL);
    
    XSelectInput(dis, win, ExposureMask|ButtonPressMask|KeyPressMask|KeyReleaseMask|StructureNotifyMask|DestroyNotify);
    gc=XCreateGC(dis, win, 0,0);

    Atom wm_delete = XInternAtom( dis, "WM_DELETE_WINDOW", 1 );
    XSetWMProtocols( dis, win, &wm_delete, 1 );

    window_framebuffer.frame = XCreatePixmap(dis, win, width, height, 24);

    //Draw Window
    XClearWindow(dis, win);
    XMapRaised(dis, win);
    
    int result;
    //Create window mutex
    result = pthread_mutex_init(&mutex, NULL);
    SMART_ASSERT(result >= 0, "Could not create widnow mutex!");

    //Check for controllers
    gpad_device_list_t controller_list = gpad_list_devices();
    gpad_device_list_ent_t* plugged_in_controller =  *((gpad_device_list_ent_t**)controller_list);
    if(plugged_in_controller != NULL){
        printf("\"%s\" plugged into controller port #1\n", plugged_in_controller->name);
        joypad_plug_in_contoller(JOYPAD_1, CONTROLLER_MODE_CONTROLLER, plugged_in_controller);
    }else{
        joypad_plug_in_contoller(JOYPAD_1, CONTROLLER_MODE_KEYBOARD, plugged_in_controller);
    }
    gpad_device_list_free(controller_list);

    //Create sound engine
    if(playback_start_audio_engine() == false){
        PRINT_ERROR("audio", "Could not start audio engine!");
    }

    //Create window thread
    result = pthread_create(&window_thread_id, NULL, window_thread, NULL);
    SMART_ASSERT(result >= 0, "ERR: Could not create window thread!");
}


static void window_flush(void){
    XLockDisplay(dis);
    XEvent event;
    memset(&event, 0, sizeof(XEvent));
    event.type = Expose;
    event.xexpose.window = win;
    XSendEvent(dis, win, false, ExposureMask, &event);
    XFlush(dis);
    XUnlockDisplay(dis);
}


#if WINDOW_ENABLE_CONTROLLER_OVERLAY
    #define WINDOW_CONTROLLER_OVERLAY_HEIGHT 25
    #define WINDOW_CONTROLLER_OVERLAY_WIDTH  70
    static unsigned int overlay_img_data[WINDOW_CONTROLLER_OVERLAY_WIDTH * WINDOW_CONTROLLER_OVERLAY_HEIGHT];
#endif

static void window_redraw(void){
    pthread_mutex_lock(&mutex);
    if(!window_framebuffer_updated){
        pthread_mutex_unlock(&mutex);
        return;
    }else{
        window_framebuffer_updated = false;
    }
    
    XLockDisplay(dis);
    XInitThreads();



    //Redraw code goes here

    if(window_framebuffer.img_dat.ximg != NULL){
        XPutImage(dis, window_framebuffer.frame, gc, (XImage*)window_framebuffer.img_dat.ximg, 0, 0, 0, 0, window_framebuffer.img_dat.width, window_framebuffer.img_dat.height);

        #if WINDOW_ENABLE_CONTROLLER_OVERLAY
            const unsigned int grey = 0xFFA0A0A0;
            const unsigned int black = 0xFF000000;
            const unsigned int red = 0xFF0000FF;
            const unsigned int magenta = 0xFFFF00FF;
            const unsigned int yellow = 0xFF00FFFF;
            for(win_size_t y = 0; y < WINDOW_CONTROLLER_OVERLAY_HEIGHT; y++){
                for(win_size_t x = 0; x < WINDOW_CONTROLLER_OVERLAY_WIDTH; x++){
                    unsigned int color = grey;
                    if (10 <= x && x <= 15 && 5 <= y && y <= 10){
                        color = joypad_read_specific_button(0, BUTTON_UP) ? red : black;
                    }
                    if (10 <= x && x <= 15 && 15 <= y && y <= 20){
                        color = joypad_read_specific_button(0, BUTTON_DOWN) ? red : black;
                    }
                    if (5 <= x && x <= 10 && 10 <= y && y <= 15){
                        color = joypad_read_specific_button(0, BUTTON_LEFT) ? red : black;
                    }
                    if (15 <= x && x <= 20 && 10 <= y && y <= 15){
                        color = joypad_read_specific_button(0, BUTTON_RIGHT) ? red : black;
                    }
                    if(25 <= x && x <= 30 && 10 <= y && y <= 13){
                        color = joypad_read_specific_button(0, BUTTON_SELECT) ? magenta : black;
                    }
                    if(35 <= x && x <= 40 && 10 <= y && y <= 13){
                        color = joypad_read_specific_button(0, BUTTON_START) ? magenta : black;
                    }
                    if(45 <= x && x <= 50 && 13 <= y && y <= 17){
                        color = joypad_read_specific_button(0, BUTTON_B) ? yellow : black;
                    }
                    if(55 <= x && x <= 60 && 13 <= y && y <= 17){
                        color = joypad_read_specific_button(0, BUTTON_A) ? yellow : black;
                    }



                    overlay_img_data[y * WINDOW_CONTROLLER_OVERLAY_WIDTH + x] = color;
                }
            }
            Image img;
            Image_create(&img, (Color*)overlay_img_data, WINDOW_CONTROLLER_OVERLAY_WIDTH, WINDOW_CONTROLLER_OVERLAY_HEIGHT);

            XPutImage(dis, window_framebuffer.frame, gc, (XImage*)img.ximg, 0, 0, 0, window_height - WINDOW_CONTROLLER_OVERLAY_HEIGHT, img.width, img.height);
            Image_destroy(&img);
        #endif
        XCopyArea(dis, window_framebuffer.frame, win, gc, 0, 0, window_framebuffer.img_dat.width, window_framebuffer.img_dat.height, WINDOW_DRAW_OFFSET_X, WINDOW_DRAW_OFFSET_Y);
    }

    XUnlockDisplay(dis);

    window_flush();
    pthread_mutex_unlock(&mutex);
}


static void window_handle_key(bool key_pressed, byte keycode){
	bool valid_key_pressed = true;

    if(joypad_get_joypad_mode(JOYPAD_1) == CONTROLLER_MODE_CONTROLLER){
        return;
    }

	switch(keycode){
		case 'w':
			joypad_set_button(JOYPAD_1, BUTTON_UP, key_pressed);
		break;
		case 'a':
			joypad_set_button(JOYPAD_1, BUTTON_LEFT, key_pressed);
		break;
		case 's':
			joypad_set_button(JOYPAD_1, BUTTON_DOWN, key_pressed);
		break;
		case 'd':
			joypad_set_button(JOYPAD_1, BUTTON_RIGHT, key_pressed);
		break;
		case ',':
			joypad_set_button(JOYPAD_1, BUTTON_A, key_pressed);
		break;
		case '.':
			joypad_set_button(JOYPAD_1, BUTTON_B, key_pressed);
		break;
		case '\'':
			joypad_set_button(JOYPAD_1, BUTTON_START, key_pressed);
		break;
		case ';':
			joypad_set_button(JOYPAD_1, BUTTON_SELECT, key_pressed);
		break;
		default:
			valid_key_pressed = false;
		break;
	}
}

static void window_handle_event(XEvent* __restrict__ event){
    //Handle keystrokes here
	char buf[128] = {0};
	KeySym keysym;
	int len;

	switch(event->type){
   		case ConfigureNotify:
	        if(event->xconfigure.width != window_width || event->xconfigure.height != window_height){
	            XResizeWindow(dis, win, window_width, window_height);
	        }
    	break;
    	case ButtonPress:
			window_mouse_x = event->xbutton.x;
			window_mouse_y = event->xbutton.y;
    	break;
    	case KeyRelease:
    	case KeyPress:
    		len = XLookupString(&event->xkey, buf, sizeof(buf) - 1, &keysym, NULL);
    		window_handle_key(event->type == KeyPress, buf[0]);
    		(void)len;
    	break;
    	default:

    	break;
	}
}

static void* window_thread(void* args){
    (void)args;
    
    XEvent event;
    while(1){
        XNextEvent(dis, &event);

        window_handle_event(&event);
        window_redraw();
    }
    return NULL;
}




void window_update_image(win_size_t width, win_size_t height, const void* __restrict image_data){
    pthread_mutex_lock(&mutex);


    if(window_framebuffer.img_dat.ximg != NULL){
        Image_destroy(&window_framebuffer.img_dat);
        window_framebuffer.img_dat.ximg = NULL;
    }

    Image_create(&window_framebuffer.img_dat, (Color*)image_data, width, height);

    window_framebuffer_updated = true;

    pthread_mutex_unlock(&mutex);

    window_redraw();
}

void window_destroy(void){
    pthread_mutex_lock(&mutex);

    pthread_cancel(window_thread_id);

    if(window_framebuffer.img_dat.ximg != NULL){
        Image_destroy(&window_framebuffer.img_dat);
    }
    XUnlockDisplay(dis);
    XFreeGC(dis, gc);
    XDestroyWindow(dis,win);
    XCloseDisplay(dis);

    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);

    playback_destroy_audio_engine();
}