#include "common.h"


#define BUS_SIZE 0xFFFF

byte debug_read_do_not_use_pls(word address);

void busWrite8(word address, word data);
word busRead8(word adress);
word busRead16(word address);
void busWrite16(word address, word data);

void dumpBus();

