#ifndef BUS_H
#define BUS_H

#include "sound.h"
#include "common.h"


#define BUS_SIZE 0xFFFF

void activateCpuNmi();

byte debug_read_do_not_use_pls(word address);

void busWrite8(word address, byte data);
byte busRead8(word adress);
word busRead16(word address);
void busWrite16(word address, word data);

void dumpBus();


void run_nes_for_x_audio_frames(size_t frame_count);
void write_audio_frames_to_soundcard(float* frames, size_t frame_count);

void shutdown_program(void);


#define ROM_VECTOR_NMI 0xFFFA
#define ROM_VECTOR_RESET 0xFFFC
#define ROM_VECTOR_IRQ 0xFFFE

#define OAM_DMA_ADDR 0x4014
#define JOYPAD1_ADDR 0x4016

#define PPU_START 0x2000
#define PPU_END 0x3FFF






#endif