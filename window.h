#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>


typedef unsigned long win_size_t;

void window_init(win_size_t width, win_size_t height);

void window_update_image(win_size_t width, win_size_t height, const void* __restrict image_data);

bool window_shutdown_triggered(void);

void window_destroy(void);



#endif