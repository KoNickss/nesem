#include "apu.h"
#include "sound.h"
#include "bus.h"

ApuInternalState apuInternalState;


PulseReg pulseReg1;
PulseReg pulseReg2;

static inline double fastsine(double x) {
    double inv_twopi = 1.0 / (2.0 * PI);
    x = x * inv_twopi;
    x = x - (int)(x + (x < 0 ? -0.5 : 0.5));
    x = x * (2.0 * PI);

    double abs_x = (x < 0) ? -x : x;
    double sin_x = (4.0 / PI) * x - (4.0 / (PI * PI)) * x * abs_x;

    double abs_sin_x = (sin_x < 0) ? -sin_x : sin_x;
    sin_x = 0.225 * (sin_x * abs_sin_x - sin_x) + sin_x;

    return sin_x;
}

static inline double fastcosine(double x) {
    return fastsine(x + PI / 2.0);
}

void apuRegWrite(byte addr_offset, byte data){
    if(addr_offset <= 0x3){
        addr_offset -= 0;
        pulseReg1.data[addr_offset] = data;
    }
    if(addr_offset > 0x3 && addr_offset <= 0x7){
        addr_offset -= 0x4;
        pulseReg2.data[addr_offset] = data;
    }
}

byte apuRegRead(byte addr_offset){
    if(addr_offset <= 0x3){
        addr_offset -= 0;
        return pulseReg1.data[addr_offset];
    }
    if(addr_offset > 0x3 && addr_offset <= 0x7){
        addr_offset -= 0x4;
        return pulseReg2.data[addr_offset];
    }


    return 0;
}

static inline word pulseRegGetTimer(PulseReg p){
    return (p.field.timerHigh << 8) | p.field.timerLow; //the fact theyre not next to eachother is so stupid, couldve easily been done with using unions/struct, instead of a getter
}

static inline double getPulseFrequencyHertz(PulseReg p){
    return (pulseRegGetTimer(p) < 8) ? (0.0) //just how the nes works
                                     : (F_CPU_NTSC/(16 * (pulseRegGetTimer(p) + 1))); //f_CPU / 16*timer
}

static inline double getPulseRegPhase(PulseReg p){
    return (p.field.duty) ? (0.25 * p.field.duty) : 0.125;
    // 0 = 12.5%
    // 1 = 25%
    // 2 = 50%
    // 3 = 75%
}

static inline double normalizeVolume(double volume){
    return volume / 15.0;
}

static inline double getSawToothFromHarmonics(double frequency, double phase, double time){
    double result = 0;

    for(int i = 1; i <= HARMONICS; ++i){
        result += (double)(fastsine( 2. * PI * i * (double)(frequency * time - phase) )) / (double)i;
    }

    //Thank you, Fourier!

    return result;
}

static inline double getPulseWaveValue(PulseReg p, double time){
    double frequency = getPulseFrequencyHertz(p);
    double phase = getPulseRegPhase(p);

    double ftime = getSawToothFromHarmonics(frequency, 0, time); //f(time) where f is a sawtooth obtained from sinusoidal harmonics
    double gtime = getSawToothFromHarmonics(frequency, phase, time); //g(time) same as f

    double sqwtime = ftime - gtime; //square wave in function of time

    //double sqwtime_w_volume = sqwtime * normalizeVolume(p.field.volume); //account for amplitude via volume

    double nes_hardware_value = ((sqwtime + 1.0) / 2.0) * p.field.volume;
    return nes_hardware_value;
}


static inline double combinePulseWavePoints(double p1x, double p2x){
    return (95.88/((double)(8128./((double)(p1x + p2x)))+100.));
}


void soundInit(){
    if(!playback_start_audio_engine()){
        DERROR("Failed to start miniaudio engine for APU!");
        return;
    }
}

void initApu(){
    apuInternalState.audio_cycle_timer = 0;
    apuInternalState.frame_counter_cycles = 0;
    apuInternalState.batch_index = 0;
    apuInternalState.sample_accumulator = 0;
    apuInternalState.stream_time = 0.0;

    for(int i = 0; i < 4; ++i){
        pulseReg1.data[i] = 0;
        pulseReg2.data[i] = 0;
    }

    //soundInit();
}



void apuClock(){

    #ifdef DEBUG
    printf("APU CLOCK!\n");
    #endif

    apuInternalState.audio_cycle_timer += 2.0; //otherwise game runs at 200% speed, idk

    if(apuInternalState.audio_cycle_timer >= CPU_CYCLES_PER_AUDIO_FRAME){
        apuInternalState.audio_cycle_timer -= CPU_CYCLES_PER_AUDIO_FRAME;

        apuInternalState.stream_time += (1.0 / 44100.0);

        if(apuInternalState.stream_time >= 7)
            apuInternalState.stream_time -= 7;

        double wavePointPulse1 = 0.0; //THIS SHOULD BE 0, IS1 FOR DEBUGGING, IF YOU SEE THIS CHANGE ME BACK!!

        if(pulseReg1.field.volume == 0 || pulseRegGetTimer(pulseReg1) < 8){
            //reset stream time on silence to prevent FP precision loss
            //apuInternalState.stream_time = 0.00001;
            wavePointPulse1 = 0.0;
            #ifdef DEBUG
            printf("--############# IM QUIET %d %d %d %d\n", pulseReg1.data[0],pulseReg1.data[1],pulseReg1.data[2],pulseReg1.data[3]);
            #endif
        } else {
            wavePointPulse1 = getPulseWaveValue(pulseReg1, apuInternalState.stream_time);
            #ifdef DEBUG
            printf("--!_!_!_!_!_!_! GOT WAVE %f\n", wavePointPulse1);
            #endif
        }

        double wavePointPulse2 = 0.0; //THIS SHOULD BE 0, IS1 FOR DEBUGGING, IF YOU SEE THIS CHANGE ME BACK!!

        if(pulseReg2.field.volume == 0 || pulseRegGetTimer(pulseReg2) < 8){
            //reset stream time on silence to prevent FP precision loss
            //apuInternalState.stream_time = 0.00001;
            wavePointPulse2 = 0.0;
            #ifdef DEBUG
            printf("--############# IM QUIET %d %d %d %d\n", pulseReg2.data[0],pulseReg2.data[1],pulseReg2.data[2],pulseReg2.data[3]);
            #endif
        } else {
            wavePointPulse2 = getPulseWaveValue(pulseReg2, apuInternalState.stream_time);
            #ifdef DEBUG
            printf("--!_!_!_!_!_!_! GOT WAVE %f\n", wavePointPulse1);
            #endif
        }

        float sample = (float)combinePulseWavePoints(wavePointPulse1, wavePointPulse2); //TODO FIXME: WE LOSE PRECISION/WASTE COMPUTATION! MAKE EVERYTHING FLOAT OR DOUBLE BUT NOT BOTH

        #ifdef DEBUG
        printf("GOT SAMPLE!! %f\n", sample);
        #endif

        apuInternalState.batch_buffer[apuInternalState.batch_index++] = sample; // Left
        apuInternalState.batch_buffer[apuInternalState.batch_index++] = sample; // Right

        //When batch buffer is full, send the chunk to the soundcard driver
        if(apuInternalState.batch_index >= (AUDIO_BATCH_FRAMES * 2)){
            write_audio_frames_to_soundcard(apuInternalState.batch_buffer, AUDIO_BATCH_FRAMES);

            #ifdef DEBUG
            for(int i = 0; i < apuInternalState.batch_index; ++i){
                printf("---------SENT SAMPLE %f\n", apuInternalState.batch_buffer[i]);
            }
            #endif

            apuInternalState.batch_index = 0;
        }
    }
}
