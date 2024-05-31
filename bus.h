#include "common.h"

#define BUS_SIZE 0xFFFF

void activateCpuNmi();

byte debug_read_do_not_use_pls(word address);

void busWrite8(word address, word data);
word busRead8(word adress);
word busRead16(word address);
void busWrite16(word address, word data);

void dumpBus();

#define ROM_VECTOR_NMI 0xFFFA
#define ROM_VECTOR_RESET 0xFFFC
#define ROM_VECTOR_IRQ 0xFFFE