#ifndef SOUND_H
#define SOUND_H


#include <stddef.h>
#include <stdbool.h>

#define CHANNEL_COUNT 2
#define PLAYBACK_BUFFER_SIZE (2048 * CHANNEL_COUNT)
#if PLAYBACK_BUFFER_SIZE < (2 * CHANNEL_COUNT)
    #error "PLAYBACK_BUFFER_SIZE is too small! Must be able to store at least 2 frames!"
#endif

//Change this to 'true' if you want the audio thread to wait for more samples to play if it runs out. This can reduce audio stutters if you have a low PLAYBACK_BUFFER_SIZE. If set to 'false' it will slightly reduce CPU usage
#define SOUND_WAIT_FOR_WRITES false
#define PLAYBACK_ENABLE_MA_RING_BUFFER true

#if !SOUND_WAIT_FOR_WRITES && PLAYBACK_ENABLE_MA_RING_BUFFER
    #define PLAYBACK_CONSUME_DROPPED_FRAMES true //Helps with syncing audio when using the MA ring buffer and you are not waiting for writes 
#else
    #define PLAYBACK_CONSUME_DROPPED_FRAMES false
#endif

typedef void** playback_device_list_t;





bool playback_start_audio_engine(void);

size_t playback_channel_write_frames(size_t channel_id, const float* frames, size_t num_frames);

void playback_channel_set_enable_state(size_t channel_id, bool state);

void playback_channel_set_volume(size_t channel_id, float volume);

void playback_destroy_audio_engine(void);



#endif