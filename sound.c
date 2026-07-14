#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "sound.h"
#include "common.h"
#include <pthread.h>
#include <stdbool.h>

#define PLAYBACK_AUDIO_FORMAT ma_format_f32

static bool audio_system_initalized = false;


#if __has_attribute(aligned)
    #define playback_sound_engine_align(a_value) __attribute__((aligned(a_value)))
#else
    #define playback_sound_engine_align(a_value)
    #pragma message("Your compiler does not support struct alignment (or I am just stupid and it is possible on your compiler!). You will maybe see a hair of degraded performance in multithreaded channel situations")
#endif


#if PLAYBACK_ENABLE_MA_RING_BUFFER
typedef struct playback_sound_engine_align(64) {
    ma_pcm_rb buffer;
    float volume;
    size_t frames_dropped;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
    bool connected;
    bool initalized;
}playback_channel_t;

#define PLAYBACK_CHANNEL_INITIALIZER     {\
        .volume=0.0f, \
        .mutex = PTHREAD_MUTEX_INITIALIZER, \
        .cond_not_full = PTHREAD_COND_INITIALIZER, \
        .cond_not_empty = PTHREAD_COND_INITIALIZER, \
        .connected = false, \
        .frames_dropped = 0, \
        .initalized = false\
    }
#else
typedef struct playback_sound_engine_align(64) {
    float* head;
    float* tail;
    float buffer[PLAYBACK_BUFFER_SIZE];
    float volume;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
    bool connected;
    bool initalized;
}playback_channel_t;

#define PLAYBACK_CHANNEL_INITIALIZER     {\
        .head=NULL, \
        .tail=NULL, \
        .volume=0.0f, \
        .mutex = PTHREAD_MUTEX_INITIALIZER, \
        .cond_not_full = PTHREAD_COND_INITIALIZER, \
        .cond_not_empty = PTHREAD_COND_INITIALIZER, \
        .connected = false, \
        .initalized = false \
    }
#endif

playback_channel_t playback_channels[] = {
    PLAYBACK_CHANNEL_INITIALIZER,
    PLAYBACK_CHANNEL_INITIALIZER,
    PLAYBACK_CHANNEL_INITIALIZER,
    PLAYBACK_CHANNEL_INITIALIZER
};

#define PLAYBACK_CHANNELS_COUNT (sizeof(playback_channels)/sizeof(playback_channel_t))

static bool playback_init_channel(playback_channel_t* chan){
    if(chan == NULL || (audio_system_initalized == false)){
        DERROR("The channel is NULL or audio system is off!");
        return false;
    }
    if(chan->initalized){
        DERROR("Could not create channel! Channel is already active!");
        return false;
    }

    playback_channel_t tmp = PLAYBACK_CHANNEL_INITIALIZER;
    *chan = tmp;

    #if !PLAYBACK_ENABLE_MA_RING_BUFFER

    chan->head = chan->buffer;
    chan->tail = chan->buffer;
    memset(chan->buffer, 0, sizeof(chan->buffer));
    #else
    if(ma_pcm_rb_init(PLAYBACK_AUDIO_FORMAT, CHANNEL_COUNT, PLAYBACK_BUFFER_SIZE/2, NULL, NULL, &chan->buffer) != 0){
        DERROR("Could not create the ring buffer for channel %lu", chan - playback_channels);
        return false;
    }
    #endif
    if(pthread_mutex_init(&chan->mutex, NULL) != 0){
        DERROR("Could not create the mutex for channel %lu", chan - playback_channels);
        return false;
    }
    if(pthread_cond_init(&chan->cond_not_empty, NULL) != 0){
        pthread_mutex_destroy(&chan->mutex);
        DERROR("Could not create the pthread cond for 'not_empty' %lu", chan - playback_channels);
        return false;
    }
    if(pthread_cond_init(&chan->cond_not_full, NULL) != 0){
        pthread_mutex_destroy(&chan->mutex);
        pthread_cond_destroy(&chan->cond_not_empty);
        DERROR("Could not create the pthread cond for 'not_full' %lu", chan - playback_channels);
        return false;
    }
    chan->initalized = true;
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
    if(chan->initalized == false){
        return 0;
    }
    #if PLAYBACK_ENABLE_MA_RING_BUFFER
        return ma_pcm_rb_available_read(&chan->buffer);
    #else
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
    #endif
}

#if !PLAYBACK_ENABLE_MA_RING_BUFFER
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
#endif


static size_t playback_channel_read_frames(playback_channel_t* chan, float* dest, size_t num_frames){
    SMART_ASSERT(dest != NULL, "Invalid dest buffer!");
    SMART_ASSERT(chan != NULL, "Invalid playback channel!");

    if(num_frames == 0 || (audio_system_initalized == false) || (chan->initalized == false)){
        return 0;
    }

    #if PLAYBACK_ENABLE_MA_RING_BUFFER
        size_t frames_read = 0;
        int num_errors = 0;

        while(frames_read < num_frames){
            SMART_ASSERT(num_errors < 10, "Too many errors on channel %lu", chan - playback_channels);
            ma_uint32 read_buffer_size_in_frames = num_frames - frames_read;
            void* read_buffer_ptr = NULL;
            if(playback_channel_get_frames_available(chan) <= 0){
                #if SOUND_WAIT_FOR_WRITES
                    pthread_mutex_lock(&chan->mutex);
                    bool connected = chan->connected;

                    while(playback_channel_get_frames_available(chan) <= 0 && connected){
                        if(chan - playback_channels == 1){
                            //printf("Waiting to read %u frames but %lu availible\n", read_buffer_size_in_frames, playback_channel_get_frames_available(chan));
                        }
                        pthread_cond_wait(&chan->cond_not_empty, &chan->mutex);
                        if(chan - playback_channels == 1){
                            //printf("Awoke %lu availible\n", playback_channel_get_frames_available(chan));
                        }
                        connected = chan->connected;
                    }
                    pthread_mutex_unlock(&chan->mutex);

                    if(connected == false){
                        return frames_read;
                    }
                #else
                    break;
                #endif
            }
            ma_result res = MA_SUCCESS;
            if((res = ma_pcm_rb_acquire_read(&chan->buffer, &read_buffer_size_in_frames, &read_buffer_ptr)) != MA_SUCCESS || read_buffer_ptr == NULL){
                DERROR("Could not read from ring buffer! err_code=%d read_buffer_ptr=%p", res, read_buffer_ptr);
                num_errors++;
                continue;
            }

            if(read_buffer_size_in_frames > 0){
                memcpy(dest, read_buffer_ptr, sizeof(float) * read_buffer_size_in_frames * CHANNEL_COUNT);
            }
            if((res = ma_pcm_rb_commit_read(&chan->buffer, read_buffer_size_in_frames)) != MA_SUCCESS){
                DERROR("Could not commit read to ring buffer! err_code=%d", res);
                num_errors++;
                continue;
            }

            frames_read += read_buffer_size_in_frames;
            dest += read_buffer_size_in_frames * CHANNEL_COUNT;

            pthread_mutex_lock(&chan->mutex);
            pthread_cond_signal(&chan->cond_not_full);
            pthread_mutex_unlock(&chan->mutex);
        }
        return frames_read;
    #else

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
    #endif
}

static size_t playback_channel_write_floats(size_t channel_id, const float* src, size_t num_floats){
    SMART_ASSERT(src != NULL, "Invalid src buffer!");
    SMART_ASSERT(channel_id < PLAYBACK_CHANNELS_COUNT, "Invalid playback channel_id of %lu", channel_id);
    size_t floats_written = 0;
    

    while(floats_written < num_floats){
        #if PLAYBACK_ENABLE_MA_RING_BUFFER
            SMART_ASSERT(false, "You should not be calling this function!");

        #else
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
    #endif
    }
    return floats_written;
}

size_t playback_channel_write_frames(size_t channel_id, const float* src, size_t num_frames){
    if(num_frames == 0 || (audio_system_initalized == false) || (playback_channels[channel_id].initalized == false)){
        return 0;
    }
    SMART_ASSERT(src != NULL, "Invalid src buffer!");
    SMART_ASSERT(channel_id < PLAYBACK_CHANNELS_COUNT, "Invalid playback channel_id of %lu", channel_id);

    #if !PLAYBACK_ENABLE_MA_RING_BUFFER
        return playback_channel_write_floats(channel_id, src, num_frames * CHANNEL_COUNT);
    #else
        size_t frames_written = 0;
        int num_errors = 0;

        #if PLAYBACK_CONSUME_DROPPED_FRAMES
            if(playback_channels[channel_id].frames_dropped > 0){
                pthread_mutex_lock(&playback_channels[channel_id].mutex);
                size_t frames_to_consume = playback_channels[channel_id].frames_dropped;
                if(frames_to_consume > num_frames){
                    frames_to_consume = num_frames;
                }
                playback_channels[channel_id].frames_dropped -= frames_to_consume;
                pthread_mutex_unlock(&playback_channels[channel_id].mutex);
                frames_written += frames_to_consume;
            }
        #endif

        while(frames_written < num_frames){
            SMART_ASSERT(num_errors < 10, "Too many errors on channel %lu", channel_id);
            playback_channel_t* chan =  &playback_channels[channel_id];
            ma_uint32 write_buffer_size_in_frames = num_frames - frames_written;
            ma_uint32 frames_free = ma_pcm_rb_available_write(&chan->buffer);

            if(frames_free <= 0){
                pthread_mutex_lock(&chan->mutex);
                while(ma_pcm_rb_available_write(&chan->buffer) <= 0 && chan->connected){
                    pthread_cond_wait(&chan->cond_not_full, &chan->mutex);
                }
                if(chan->connected == false){
                    pthread_mutex_unlock(&chan->mutex);
                    return frames_written;
                }
                pthread_mutex_unlock(&chan->mutex);
                continue;
            }

            void* write_buffer_ptr = NULL;
            if(ma_pcm_rb_acquire_write(&chan->buffer, &write_buffer_size_in_frames, &write_buffer_ptr) != MA_SUCCESS || write_buffer_ptr == NULL){
                DERROR("Could not write to ring buffer!");
                num_errors++;
                continue;
            }

            if(write_buffer_size_in_frames > 0){
                memcpy(write_buffer_ptr, src + (frames_written * CHANNEL_COUNT), sizeof(float) * write_buffer_size_in_frames * CHANNEL_COUNT);
            }

            if(ma_pcm_rb_commit_write(&chan->buffer, write_buffer_size_in_frames) != MA_SUCCESS){
                DERROR("Could not commit write to ring buffer!");
                num_errors++;
                continue;
            }

            frames_written += write_buffer_size_in_frames;

            #if SOUND_WAIT_FOR_WRITES
            if(write_buffer_size_in_frames > 0){
                pthread_mutex_lock(&chan->mutex);
                pthread_cond_signal(&chan->cond_not_empty);
                pthread_mutex_unlock(&chan->mutex);
            }
            #endif
        }
        return frames_written;
    #endif
}


static void playback_channel_destroy(playback_channel_t* chan){
    SMART_ASSERT(chan != NULL, "Channel was NULL!");
    if(chan->initalized == false){
        DWARN("Attempting to destroy a channel that is already destroyed");
        return;
    }
    playback_channel_set_enable_state(chan - playback_channels, false);
    
    pthread_mutex_lock(&chan->mutex);
    pthread_cond_destroy(&chan->cond_not_empty);
    pthread_cond_destroy(&chan->cond_not_full);
    chan->initalized = false;
    pthread_mutex_unlock(&chan->mutex);

    pthread_mutex_destroy(&chan->mutex);
    

    #if PLAYBACK_ENABLE_MA_RING_BUFFER
        ma_pcm_rb_uninit(&chan->buffer);
    #endif
}


static float _data_callback_fbuffer[PLAYBACK_BUFFER_SIZE];
#if !PLAYBACK_ENABLE_MA_RING_BUFFER
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
                    pthread_cond_wait(&playback_channels[i].cond_not_empty, &playback_channels[i].mutex);
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
                if(framesToRead >= sizeof(_data_callback_fbuffer)/sizeof(float)/CHANNEL_COUNT){
                    framesToRead = sizeof(_data_callback_fbuffer)/sizeof(float)/CHANNEL_COUNT;
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
#else
static void data_callback(ma_device* __restrict__ pDevice, void* __restrict__ pOutput, const void* __restrict__ pInput, ma_uint32 frameCount)
{
    (void)pDevice;

    const size_t TEMP_DATA_BUFFER_SIZE_IN_FRAMES = (sizeof(_data_callback_fbuffer)/sizeof(float))/CHANNEL_COUNT;
    

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
            
            if(playback_channels[i].connected == false || playback_channels[i].initalized == false){
                continue;
            }
            
            volume = playback_channels[i].volume;

            if(totalFramesRead < frameCount) {
                ma_uint64 totalFramesRemaining = frameCount - totalFramesRead;
                ma_uint64 framesToRead = totalFramesRemaining;
                if(framesToRead >= TEMP_DATA_BUFFER_SIZE_IN_FRAMES){
                    framesToRead = TEMP_DATA_BUFFER_SIZE_IN_FRAMES;
                }
                ma_uint64 framesReadThisIteration = playback_channel_read_frames(&playback_channels[i], _data_callback_fbuffer, framesToRead);
                
                #if SOUND_WAIT_FOR_WRITES == false
                    if(framesReadThisIteration == 0){
                        framesReadThisIteration = framesToRead;
                        DWARN("Emulator is lagging! Audio is being faded to compensate");
                        float fade_volume = 1.0f;
                        float fade_step = 1.0f/((float)framesToRead);
                        float* last_good_frame = &((float*)pOutput)[(totalFramesRead == 0) ? 0 : ((totalFramesRead-1)*CHANNEL_COUNT)];
                        for(int iFrame = 0; iFrame < framesToRead; iFrame++){
                            fade_volume -= fade_step;
                            _data_callback_fbuffer[iFrame*CHANNEL_COUNT + 0] = last_good_frame[0] * fade_volume; 
                            _data_callback_fbuffer[iFrame*CHANNEL_COUNT + 1] = last_good_frame[1] * fade_volume; 
                        }
                        
                        pthread_mutex_lock(&playback_channels[i].mutex);
                        playback_channels[i].frames_dropped += framesToRead;
                        pthread_mutex_unlock(&playback_channels[i].mutex);
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
            }

        }
    }while(frames_left);
    
    (void)pInput;
}


#endif


#define ENABLE_AUDIO_TESTER true

#if ENABLE_AUDIO_TESTER == true


void* audio_thread(void* args){

    size_t channel_id = *(size_t*)args;
    free(args);

    playback_channel_set_enable_state(channel_id, true);
    playback_channel_set_volume(channel_id, 1.0f);

    FILE* f;
    if(channel_id == 0){
        f = fopen("/dev/shm/0.RAW", "rb");
    }else{
        f = fopen("/dev/shm/1.RAW", "rb");
    }
    if(f == NULL){
        playback_channel_set_enable_state(channel_id, false);
        playback_channel_destroy(&playback_channels[channel_id]);
        PRINT_ERROR("audio_test", "Could not open test file");
        return NULL;
    }

    float buffer[4000];
    while (true) {
        ssize_t bytes_read = fread(buffer, sizeof(float), sizeof(buffer)/sizeof(float), f);
        if(bytes_read <= 0){
            fseek(f, 0, SEEK_SET);
            continue;
        }

        //Zero out either left or right ear depending on channel
        for(int i = channel_id; i < sizeof(buffer)/sizeof(float); i+=2){
            buffer[i] = 0;
        }

        if(channel_id > 0){
            usleep((rand() % 1) +100);
        }

        playback_channel_write_frames(channel_id, buffer, sizeof(buffer)/sizeof(float)/2);
    }

    return NULL;
}

#endif


static ma_device _audio_device;
bool playback_start_audio_engine(void){
    //Make sure audio engine is not running
    playback_destroy_audio_engine();

    audio_system_initalized = true;

    for(size_t i = 0; i < PLAYBACK_CHANNELS_COUNT; i++){
        playback_init_channel(&playback_channels[i]);
    }

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
        audio_system_initalized = false;
        for(size_t i = 0; i < PLAYBACK_CHANNELS_COUNT; i++){
            playback_channel_destroy(&playback_channels[i]);
        }
        return false;
    }

    if (ma_device_start(&_audio_device) != MA_SUCCESS) {
        PRINT_ERROR("audio", "Failed to start playback device.\n");
        playback_destroy_audio_engine();
        return false;
    }


    


    #if ENABLE_AUDIO_TESTER == true
    pthread_t th;
    size_t* c0 = xmalloc(sizeof(size_t));
    size_t* c1 = xmalloc(sizeof(size_t));
    *c0 = 0;
    *c1 = 1;
    pthread_create(&th, NULL, audio_thread, c0);
    pthread_create(&th, NULL, audio_thread, c1);
    #endif

    

    return true;
}


void playback_destroy_audio_engine(void){
    if(audio_system_initalized){
        for(size_t i = 0; i < PLAYBACK_CHANNELS_COUNT; i++){
            if(playback_channel_get(i)->initalized){
                playback_channel_destroy(playback_channel_get(i));
            }
        }

        ma_device_uninit(&_audio_device);
        audio_system_initalized = false;
    }
}