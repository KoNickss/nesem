#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>

const static inline unsigned int convert_rgba_to_bgra(unsigned int c){
    return (c & 0xFF00FF00 | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000));
}

typedef unsigned long win_size_t;

void window_init(win_size_t width, win_size_t height);

void window_update_image(win_size_t width, win_size_t height, const void* __restrict image_data);

void window_get_input(void);

bool window_shutdown_triggered(void);

void window_destroy(void);



#endif