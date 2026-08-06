#include "common.h"

//HELLA MAGIC NUMBERS!!

#define F_CPU_NTSC (double)1789773.0 //Hertz not MegaHertz!
#define PI (double)3.141592653589793 //yummy!
#define CPU_CYCLES_PER_AUDIO_FRAME 40.5844f
#define AUDIO_BATCH_FRAMES 64

//COOL FAST MATH!

static inline double fastcosine(double x);
static inline double fastsine(double x);

//SETTING DIALS OF SORTS

#define HARMONICS 10

typedef struct{
    double audio_cycle_timer;
    double sample_accumulator;
    int sample_count;
    int frame_counter_cycles;
    float batch_buffer[AUDIO_BATCH_FRAMES * 2];
    unsigned long batch_index;
    double stream_time;
}ApuInternalState;

typedef union{

    struct __attribute__((packed, aligned(1))){
        byte duty : 2; //duty cycle, expressed in increments of 1/4
        byte loop : 1; //play infinitely
        byte contantVol : 1; //constant volume or decrease envelope from 15 to 0
        byte volume : 4;

        byte enabled : 1;
        byte period : 3;
        byte negate : 1;
        byte shift : 3;

        byte timerLow;

        byte lengthCounterLoad : 5;
        byte timerHigh : 3;
    }field;

    byte data[4];

}PulseReg;

byte apuRegRead(byte addr_offset);
void apuRegWrite(byte addr_offset, byte data);
void initApu();
void apuClock();
