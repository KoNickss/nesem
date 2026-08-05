#ifndef SOUND_H
#define SOUND_H


#include <stddef.h>
#include <stdbool.h>

#define CHANNEL_COUNT 2
#define PLAYBACK_BUFFER_SIZE (2048 * CHANNEL_COUNT)
#if PLAYBACK_BUFFER_SIZE < (10 * CHANNEL_COUNT)
    #error "PLAYBACK_BUFFER_SIZE is too small! Must be able to store at least 10 frames!"
#endif



bool playback_start_audio_engine(void);

#if 1
//Only give access to bus.c to this function
    size_t playback_write_frames(float* frames, unsigned int frame_count);
#endif

void playback_destroy_audio_engine(void);



#endif
