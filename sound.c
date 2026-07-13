#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "sound.h"
#include "common.h"
#include <pthread.h>
#include <stdbool.h>



#if __has_attribute(aligned)
    #define playback_sound_engine_align(a_value) __attribute__((aligned(a_value)))
#else
    #define playback_sound_engine_align(a_value)
    #pragma message("Your compiler does not support struct alignment (or I am just stupid and it is possible on your compiler!). You will maybe see a hair of degraded performance in multithreaded channel situations")
#endif


typedef struct playback_sound_engine_align(64) {
    float* head;
    float* tail;
    float buffer[PLAYBACK_BUFFER_SIZE];
    float volume;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
    bool connected;
}playback_channel_t;

#define PLAYBACK_CHANNEL_INITIALIZER     {\
        .head=NULL, \
        .tail=NULL, \
        .volume=0.0f, \
        .mutex = PTHREAD_MUTEX_INITIALIZER, \
        .cond_not_full = PTHREAD_COND_INITIALIZER, \
        .cond_not_empty = PTHREAD_COND_INITIALIZER, \
        .connected = false \
    }


playback_channel_t playback_channels[] = {
    PLAYBACK_CHANNEL_INITIALIZER,
    PLAYBACK_CHANNEL_INITIALIZER,
    PLAYBACK_CHANNEL_INITIALIZER,
    PLAYBACK_CHANNEL_INITIALIZER
};

#define PLAYBACK_CHANNELS_COUNT (sizeof(playback_channels)/sizeof(playback_channel_t))

static bool playback_init_channel(playback_channel_t* chan){
    if(chan == NULL){
        DERROR("The channel is NULL!");
        return false;
    }

    playback_channel_t tmp = PLAYBACK_CHANNEL_INITIALIZER;
    *chan = tmp;

    chan->head = chan->buffer;
    chan->tail = chan->buffer;
    if(pthread_mutex_init(&chan->mutex, NULL) != 0){
        DERROR("Could not create the mutex for channel %lu", chan-playback_channels);
        return false;
    }
    if(pthread_cond_init(&chan->cond_not_empty, NULL) != 0){
        pthread_mutex_destroy(&chan->mutex);
        DERROR("Could not create the pthread cond for 'not_empty' %lu", chan-playback_channels);
        return false;
    }
    if(pthread_cond_init(&chan->cond_not_full, NULL) != 0){
        pthread_mutex_destroy(&chan->mutex);
        pthread_cond_destroy(&chan->cond_not_empty);
        DERROR("Could not create the pthread cond for 'not_full' %lu", chan-playback_channels);
        return false;
    }
    memset(chan->buffer, 0, sizeof(chan->buffer));
    return true;
}


static inline playback_channel_t* playback_channel_get(size_t index){
    return &playback_channels[index];
}


void playback_channel_set_enable_state(size_t channel_id, bool state){
    pthread_mutex_lock(&playback_channels[channel_id].mutex);
    bool old_state = playback_channels[channel_id].connected;
    playback_channels[channel_id].connected = state;

    if(old_state == true && state == false){
        //Tell the audio reader/writer to give up on any reads/writer.
        pthread_cond_signal(&playback_channels[channel_id].cond_not_empty);
        pthread_cond_signal(&playback_channels[channel_id].cond_not_full);
    }
    
    pthread_mutex_unlock(&playback_channels[channel_id].mutex);
}

void playback_channel_set_volume(size_t channel_id, float volume){
    pthread_mutex_lock(&playback_channels[channel_id].mutex);
    playback_channels[channel_id].volume = volume;
    pthread_mutex_unlock(&playback_channels[channel_id].mutex);
}


static size_t playback_channel_get_frames_available(playback_channel_t* chan){
    pthread_mutex_lock(&chan->mutex);
    size_t ret = 0;
    if(chan->head >= chan->tail){
        ret = (size_t)(chan->head - chan->tail);
        pthread_mutex_unlock(&chan->mutex);
        return ret;
    }else{
        const float* buffer_end = chan->buffer + (sizeof(chan->buffer)/sizeof(float)); 
        ret = ((size_t)(buffer_end - chan->tail)) + (size_t)(chan->head - chan->buffer);
        pthread_mutex_unlock(&chan->mutex);
        return ret;
    }
    pthread_mutex_unlock(&chan->mutex);
    return ret;
}


static  float* _playback_channel_add_to_cursor(const playback_channel_t* chan, const float* cursor, size_t amount){
    SMART_ASSERT(cursor != NULL, "Invalid cursor pointer!");
    SMART_ASSERT(chan != NULL, "Invalid playback channel!");

    const float* buffer_end = chan->buffer + PLAYBACK_BUFFER_SIZE; 
    cursor += amount;
    while(cursor >= buffer_end){
        cursor = chan->buffer + (cursor - buffer_end);
        SMART_ASSERT(cursor >= chan->buffer, "Head or tail cursor just went OOB! buffer=%p, h=%p, t=%p, c=%p", chan->buffer, chan->head, chan->tail, cursor);
    }
    return (float*)cursor;
}


static size_t playback_channel_read_frames(playback_channel_t* chan, float* dest, size_t num_frames){
    SMART_ASSERT(dest != NULL, "Invalid dest buffer!");
    SMART_ASSERT(chan != NULL, "Invalid playback channel!");

    if(num_frames == 0){
        return 0;
    }

    pthread_mutex_lock(&chan->mutex);
    if(chan->head == chan->tail){
        pthread_mutex_unlock(&chan->mutex);
        return 0;
    }

    size_t floats_requested = num_frames * CHANNEL_COUNT;

    if(chan->head >= chan->tail){
        size_t floats_to_read = (size_t)(chan->head - chan->tail);
        if(floats_to_read > floats_requested){
            floats_to_read = floats_requested;
        }
        //Only read an even amount of floats
        floats_to_read &= ~0b1;
        memcpy(dest, chan->tail, sizeof(float) * floats_to_read);

        chan->tail = _playback_channel_add_to_cursor(chan, chan->tail, floats_to_read);
        pthread_mutex_unlock(&chan->mutex);
        pthread_cond_signal(&chan->cond_not_full); //Let other writing threads know that there is more free bytes to save to
        return floats_to_read;
    }else{
        const float* buffer_end = chan->buffer + PLAYBACK_BUFFER_SIZE; 
        size_t left_size = (size_t)(buffer_end - chan->tail);
        size_t right_size = (size_t)(chan->head - chan->buffer);

        //Check if we need to reduce the size to be even amount
        if((left_size + right_size) & 1){
            if(right_size > 0){
                right_size--;
            }else{
                left_size--;
            }
        }

        if((left_size + right_size) > floats_requested){
            size_t frames_to_subtract = (left_size + right_size) - floats_requested;
            if(right_size >= frames_to_subtract){
                right_size -= frames_to_subtract;
            }else{
                frames_to_subtract -= right_size;
                right_size = 0;
                left_size -= frames_to_subtract;
            }
        }

        memcpy(dest, chan->tail, left_size * sizeof(float));
        chan->tail = _playback_channel_add_to_cursor(chan, chan->tail, left_size);

        if(right_size > 0){
            memcpy(dest+left_size, chan->buffer, right_size * sizeof(float));
            chan->tail = _playback_channel_add_to_cursor(chan, chan->tail, right_size);
        }

        pthread_mutex_unlock(&chan->mutex);
        pthread_cond_signal(&chan->cond_not_full); //Let other writing threads know that there is more free bytes to save to
        return left_size + right_size;
    }
}

static size_t playback_channel_write_floats(size_t channel_id, const float* src, size_t num_floats){
    SMART_ASSERT(src != NULL, "Invalid src buffer!");
    SMART_ASSERT(channel_id < PLAYBACK_CHANNELS_COUNT, "Invalid playback channel_id of %lu", channel_id);
    size_t floats_written = 0;
    

    while(floats_written < num_floats){
        pthread_mutex_lock(&playback_channels[channel_id].mutex);
        playback_channel_t* chan =  &playback_channels[channel_id];
        const float* BUFFER_END = chan->buffer + PLAYBACK_BUFFER_SIZE;


        if(playback_channels[channel_id].connected == false){
            //Reject writes to disabled channel
            pthread_mutex_unlock(&chan->mutex);
            return floats_written;
        }

        while(_playback_channel_add_to_cursor(chan, chan->head, 1) == chan->tail){
            //Wait for free data to become available
            pthread_cond_wait(&chan->cond_not_full, &chan->mutex);
            if(chan->connected == false){
                //If the channel was disconnected during a write, abort saving the floats
                break;
            }
        }
        //If the channel was disconnected during a write, abort saving the floats
        if(chan->connected == false){
            pthread_mutex_unlock(&chan->mutex);
            break;
        }

        size_t floats_free = 0;
        if(chan->tail <= chan->head){
            floats_free = (chan->tail - chan->buffer) + (BUFFER_END - chan->head) - 1;
        }else {
            floats_free = (chan->tail - chan->head) - 1;
        }

        while(floats_free > 0 && floats_written < num_floats){
            size_t floats_remaining = num_floats - floats_written;
            if(chan->tail <= chan->head){
                size_t floats_written_this_iter = BUFFER_END - chan->head;
                if(floats_written_this_iter > floats_free){
                    floats_written_this_iter = floats_free;
                }
                if(floats_written_this_iter >floats_remaining){
                    floats_written_this_iter = floats_remaining;
                }

                memcpy(chan->head, src + floats_written, floats_written_this_iter * sizeof(float));
                chan->head = _playback_channel_add_to_cursor(chan, chan->head, floats_written_this_iter);

                floats_written += floats_written_this_iter;
                floats_free -= floats_written_this_iter;
                //If head has looped around to the other side, we will be able to write to the other side on the next loop.
                //This is done to reuse logic and reduce code size
            }else{
                size_t floats_written_this_iter = floats_free;
                if(floats_written_this_iter > floats_remaining){
                    floats_written_this_iter = floats_remaining;
                }

                memcpy(chan->head, src + floats_written, floats_written_this_iter * sizeof(float));
                chan->head = _playback_channel_add_to_cursor(chan, chan->head, floats_written_this_iter);

                floats_written += floats_written_this_iter;
                floats_free -= floats_written_this_iter;
            }
        }


        pthread_mutex_unlock(&chan->mutex);
        pthread_cond_signal(&chan->cond_not_empty); //Signal to reader that data is ready to be read

        //If there are no floats left in the buffer to write, stop queuing writes
        if(!(floats_written < num_floats && floats_written > 0)){
            break;
        }
    }
    return floats_written;
}

size_t playback_channel_write_frames(size_t channel_id, const float* src, size_t num_frames){
    if(num_frames == 0){
        return 0;
    }
    SMART_ASSERT(src != NULL, "Invalid src buffer!");
    SMART_ASSERT(channel_id < PLAYBACK_CHANNELS_COUNT, "Invalid playback channel_id of %lu", channel_id);

    return playback_channel_write_floats(channel_id, src, num_frames * CHANNEL_COUNT);
}


static void playback_channel_destroy(playback_channel_t* chan){
    SMART_ASSERT(chan != NULL, "Channel was NULL!");
    pthread_mutex_destroy(&chan->mutex);
    pthread_cond_destroy(&chan->cond_not_empty);
    pthread_cond_destroy(&chan->cond_not_full);
}


static float _data_callback_fbuffer[PLAYBACK_BUFFER_SIZE];
static void data_callback(ma_device* __restrict__ pDevice, void* __restrict__ pOutput, const void* __restrict__ pInput, ma_uint32 frameCount)
{
    (void)pDevice;
    

    ma_uint32 frames_remaining[PLAYBACK_CHANNELS_COUNT];
    bool previous_read_failed[PLAYBACK_CHANNELS_COUNT];
    for(size_t i = 0; i <  PLAYBACK_CHANNELS_COUNT; i++){
        frames_remaining[i] = frameCount;
        previous_read_failed[i] = false;
    }

    bool frames_left = false;
    memset(_data_callback_fbuffer, 0, sizeof(_data_callback_fbuffer));
    do{
        frames_left = false;
        for(unsigned int i = 0; i < PLAYBACK_CHANNELS_COUNT; i++){
            float volume = 0.0f;
            ma_uint64 totalFramesRead = frameCount - frames_remaining[i];
            
            pthread_mutex_lock(&playback_channels[i].mutex);
            if(playback_channels[i].connected == false){
                pthread_mutex_unlock(&playback_channels[i].mutex);
                continue;
            }
            
            #if SOUND_WAIT_FOR_WRITES
            if(previous_read_failed[i]){
                bool channel_disabled_during_read = false;
                while(playback_channels[i].head == playback_channels[i].tail){
                    //Have the OS tell us when there is data to read so we are not doing 'busy waiting'
                    puts("waiting for bytes to be written");
                    pthread_cond_wait(&playback_channels[i].cond_not_empty, &playback_channels[i].mutex);
                    puts("bytes written");
                    if(playback_channels[i].connected == false){
                        //Edge case just in case the channel was disabled when it was waiting for bytes
                        channel_disabled_during_read = false;
                        break;
                    }
                }
                if(channel_disabled_during_read){
                    pthread_mutex_unlock(&playback_channels[i].mutex);
                    continue;
                }
            }
            #endif
            volume = playback_channels[i].volume;
            pthread_mutex_unlock(&playback_channels[i].mutex);

            if(totalFramesRead < frameCount) {
                ma_uint64 totalFramesRemaining = frameCount - totalFramesRead;
                ma_uint64 framesToRead = totalFramesRemaining;
                if(framesToRead >= sizeof(_data_callback_fbuffer)/sizeof(float)){
                    framesToRead = sizeof(_data_callback_fbuffer)/sizeof(float);
                }
                ma_uint64 framesReadThisIteration = playback_channel_read_frames(&playback_channels[i], _data_callback_fbuffer, framesToRead) / 2;
                
                #if SOUND_WAIT_FOR_WRITES == false
                    if(framesReadThisIteration == 0){
                        //This has the effect of just reading zeros as the current frame to play
                        framesReadThisIteration = framesToRead;
                        DWARN("Emulator is lagging! Audio is being filled with zeros to compensate");
                    }
                #endif

                /* Mix the frames together. */
                for (ma_uint64 iSample = 0; iSample < framesReadThisIteration*CHANNEL_COUNT; ++iSample) {
                    ((float*)pOutput)[totalFramesRead*CHANNEL_COUNT + iSample] += _data_callback_fbuffer[iSample] * volume;
                }

                frames_remaining[i] -= framesReadThisIteration;
                if(frames_remaining[i] > 0){
                    frames_left |= true;
                }
                #if SOUND_WAIT_FOR_WRITES == true
                    previous_read_failed[i] = (framesReadThisIteration == 0);
                #endif
            }

        }
    }while(frames_left);
    
    (void)pInput;
}


#define ENABLE_AUDIO_TESTER true

#if ENABLE_AUDIO_TESTER == true


void* audio_thread(void* args){
    FILE* f = fopen("/dev/shm/1.RAW", "rb");

    float buffer[8048];
    while (true) {
        ssize_t bytes_read = fread(buffer, sizeof(float), sizeof(buffer)/sizeof(float), f);
        if(bytes_read <= 0){
            fseek(f, 0, SEEK_SET);
            continue;
        }

        playback_channel_write_frames(0, buffer, sizeof(buffer)/sizeof(float)/2);
    }
}

#endif


static ma_device _audio_device;
bool playback_start_audio_engine(void){
    ma_result result;
    ma_device_config deviceConfig;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = CHANNEL_COUNT;
    deviceConfig.sampleRate        = ma_standard_sample_rate_44100;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = NULL;

    if (ma_device_init(NULL, &deviceConfig, &_audio_device) != MA_SUCCESS) {
        PRINT_ERROR("audio", "Failed to open playback device.\n");
        return false;
    }

    if (ma_device_start(&_audio_device) != MA_SUCCESS) {
        PRINT_ERROR("audio", "Failed to start playback device.\n");
        ma_device_uninit(&_audio_device);
        return false;
    }

    for(size_t i = 0; i < PLAYBACK_CHANNELS_COUNT; i++){
        playback_init_channel(&playback_channels[i]);
    }

    #if ENABLE_AUDIO_TESTER == true
    playback_channel_set_enable_state(0, true);
    playback_channel_set_volume(0, 1.0f);

    pthread_t th;
    pthread_create(&th, NULL, audio_thread, NULL);
    #endif

    return true;
}


void playback_destroy_audio_engine(void){
    for(size_t i = 0; i < PLAYBACK_CHANNELS_COUNT; i++){
        playback_channel_destroy(playback_channel_get(i));
    }

    ma_device_uninit(&_audio_device);
}