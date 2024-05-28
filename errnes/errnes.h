#pragma once
#include "bus.h"

#define FILE_COUNT 4
static const char* files[FILE_COUNT] = {
	"UNKNOWN_FILE",
  	"bus.c",
  	"cpu.c",
  	"ppu.c",
};

enum ERROR_TYPES{
  	UNINIT_ERR,
	INTERNAL_ERR,
  	NO_FILE,
  	BAD_FORMAT,
  	BAD_MAPPER,
  	UNKNOWN_ERR,
};

extern const unsigned char* gerrnesData;
extern unsigned int gerrnesSize;

#define ERR_ADDR 0x40
#define FILE_NAME_INDEX_ADDR 0x41
#define LINE_NUM_ADDR 0x42

inline static void errnes_set_err(unsigned char err){
  	busWrite8(ERR_ADDR, err);
}

inline static void errnes_set_line_num(unsigned int line_num){
 	busWrite16(LINE_NUM_ADDR, line_num);	
}

inline static void errnes_set_file_name(unsigned char index){
	busWrite8(FILE_NAME_INDEX_ADDR, index);
}
