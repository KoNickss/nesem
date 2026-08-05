//Miniaudio settings
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_ALSA
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_JACK
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#define MA_NO_NODE_GRAPH
/////
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "sound.h"
#include "common.h"
#include "bus.h"

#include <pthread.h>
#include <stdbool.h>

#define PLAYBACK_AUDIO_FORMAT ma_format_f32

static bool audio_system_initalized = false;



static ma_pcm_rb _audio_frame_buffer;

size_t playback_write_frames(float* frames, unsigned int frame_count){
    ma_uint32 num_frames = frame_count;
    float* src = frames;
    if(num_frames == 0 || (audio_system_initalized == false)){
        #ifdef DEBUG
        printf("IM TURNED OFF! (audio system)\n");
        #endif
        return 0;
    }
    size_t frames_written = 0;

    while(frames_written < num_frames){
        ma_uint32 write_buffer_size_in_frames = num_frames - frames_written;
        ma_uint32 frames_free = ma_pcm_rb_available_write(&_audio_frame_buffer);

        if(frames_free <= 0){
            abort();
            continue;
        }

        void* write_buffer_ptr = NULL;
        if(ma_pcm_rb_acquire_write(&_audio_frame_buffer, &write_buffer_size_in_frames, &write_buffer_ptr) != MA_SUCCESS || write_buffer_ptr == NULL){
            DERROR("Could not write to ring buffer!");
            continue;
        }

        if(write_buffer_size_in_frames > 0){
            memcpy(write_buffer_ptr, src + (frames_written * CHANNEL_COUNT), sizeof(float) * write_buffer_size_in_frames * CHANNEL_COUNT);
        }

        if(ma_pcm_rb_commit_write(&_audio_frame_buffer, write_buffer_size_in_frames) != MA_SUCCESS){
            DERROR("Could not commit write to ring buffer!");
            continue;
        }

        frames_written += write_buffer_size_in_frames;
    }
    #ifdef DEBUG
    printf("WROTE %d FRAMES\n", frames_written);
    #endif
    return frames_written;
}


static inline ma_uint32 _playback_get_available_frames(void){
    return ma_pcm_rb_available_read(&_audio_frame_buffer);
}

static size_t _playback_read_frames(float* dest, size_t num_frames){
    size_t frames_read = 0;

    while(frames_read < num_frames){
        ma_uint32 read_buffer_size_in_frames = num_frames - frames_read;
        void* read_buffer_ptr = NULL;
        if(_playback_get_available_frames() <= 0){
            //return frames_read;
            abort();
        }
        ma_result res = MA_SUCCESS;
        if((res = ma_pcm_rb_acquire_read(&_audio_frame_buffer, &read_buffer_size_in_frames, &read_buffer_ptr)) != MA_SUCCESS || read_buffer_ptr == NULL){
            DERROR("Could not read from ring buffer! err_code=%d read_buffer_ptr=%p", res, read_buffer_ptr);
            continue;
        }

        if(read_buffer_size_in_frames > 0){
            memcpy(dest, read_buffer_ptr, sizeof(float) * read_buffer_size_in_frames * CHANNEL_COUNT);
        }
        if((res = ma_pcm_rb_commit_read(&_audio_frame_buffer, read_buffer_size_in_frames)) != MA_SUCCESS){
            DERROR("Could not commit read to ring buffer! err_code=%d", res);
            continue;
        }

        frames_read += read_buffer_size_in_frames;
        dest += read_buffer_size_in_frames * CHANNEL_COUNT;
    }
    #ifdef DEBUG
    printf("GOT %d FRAMES\n", frames_read);
    #endif
    return frames_read;
}



#define READ_CHUNKS (3)
static void data_callback(ma_device* __restrict__ pDevice, void* __restrict__ pOutput, const void* __restrict__ pInput, ma_uint32 frameCount)
{
    (void)pDevice;

    ma_uint32 frames_remaining = (ma_uint32)frameCount;

    while(frames_remaining > 0){
        ma_uint32 totalFramesRead = frameCount - frames_remaining;
        if(totalFramesRead >= frameCount) {
            break;
        }


        ma_uint32 totalFramesRemaining = frameCount - totalFramesRead;
        ma_uint32 framesToRead = totalFramesRemaining;
        ma_uint32 frames_to_read_this_iter = 0;
        ma_uint32 frames_read_this_call = 0;

        for(int i = 0; i < READ_CHUNKS-1; i++){
            frames_to_read_this_iter = framesToRead/READ_CHUNKS;
            run_nes_for_x_audio_frames(frames_to_read_this_iter);
            frames_read_this_call = _playback_read_frames(&((float*)pOutput)[totalFramesRead*CHANNEL_COUNT], frames_to_read_this_iter);
            frames_remaining -= frames_read_this_call;
            totalFramesRead += frames_read_this_call;
        }
        frames_to_read_this_iter = frames_remaining;
        run_nes_for_x_audio_frames(frames_to_read_this_iter);
        frames_remaining = _playback_read_frames(&((float*)pOutput)[totalFramesRead*CHANNEL_COUNT], frames_to_read_this_iter);
        totalFramesRead += frames_read_this_call;

        //printf("FramesReadThisIteration=%u\n", framesReadThisIteration);
    }


    (void)pInput;
}




static ma_device _audio_device;
bool playback_start_audio_engine(void){
    //Make sure audio engine is not running
    playback_destroy_audio_engine();

    audio_system_initalized = true;


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
        return false;
    }

    if(ma_pcm_rb_init(PLAYBACK_AUDIO_FORMAT, CHANNEL_COUNT, PLAYBACK_BUFFER_SIZE/CHANNEL_COUNT, NULL, NULL, &_audio_frame_buffer) != 0){
        playback_destroy_audio_engine();
        DERROR("Could not create the ring buffer!");
        return false;
    }

    if (ma_device_start(&_audio_device) != MA_SUCCESS) {
        PRINT_ERROR("audio", "Failed to start playback device.\n");
        playback_destroy_audio_engine();
        return false;
    }



    return true;
}


void playback_destroy_audio_engine(void){
    if(audio_system_initalized){
        ma_device_uninit(&_audio_device);
        audio_system_initalized = false;
    }
}
