#include <stdio.h>
#include <stdlib.h>
#include "common.h"


#define BUS_SIZE 0xFFFF

byte debug_read_do_not_use_pls(word address);

void bus_write8(word address, word data);
word bus_read8(word adress);
word bus_read16(word address);
void bus_write16(word address, word data);

void dump_bus();
word mapper_resolve(word address); //gets defined in the cartridge mapper circuit code bcuz it differs from cart to cart

