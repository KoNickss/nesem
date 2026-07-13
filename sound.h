#ifndef SOUND_H
#define SOUND_H


#include <stddef.h>
#include <stdbool.h>

#define CHANNEL_COUNT 2
#define PLAYBACK_BUFFER_SIZE (2048 * CHANNEL_COUNT)
#if PLAYBACK_BUFFER_SIZE < (2 * CHANNEL_COUNT)
    #error "PLAYBACK_BUFFER_SIZE is too small! Must be able to store at least 2 frames!"
#endif

//Change this to 'true' if you want the emulator to lag while it is waiting for more samples to play. This is usually desirable since audio stutters are worse than FPS drops
#define SOUND_WAIT_FOR_WRITES true




bool playback_start_audio_engine(void);

size_t playback_channel_write_frames(size_t channel_id, const float* frames, size_t num_frames);

void playback_channel_set_enable_state(size_t channel_id, bool state);

void playback_channel_set_volume(size_t channel_id, float volume);

void playback_destroy_audio_engine(void);



#endif