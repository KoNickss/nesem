#include "bus.h"
#include <time.h>
#include <string.h>
#include <unistd.h>

//For displaying to the screen
#include "window.h"


unsigned char bus[BUS_SIZE]; //this is the bus array

//Importing the different electrical components of the NES as definitions, not yet docked to the bus

#include "cpu.h"

#include "ppu.h"

#include "cartridge.h"

#include "controller.h"

#include "window.h"

#define TRANSLATE_PPU_ADDRESS(address) ((address - PPU_START) % 8 + PPU_START)

int cpu_timeout_cycles = 0;

void performOamDMA(byte pageID){
    word startAddr = pageID << 8;

    byte page[256];

    for(word i = 0; i < sizeof(page); ++i){
        page[i] = busRead8(startAddr + i);
    }

    ppuSwallowOAMDMA(page);
}

void busWrite8(word address, byte data){

    if(cart_PRG_Write(address, data)){ //first thing we do is we hand the operation to the mapper to resolve any cartridge-side bank switching and mirroring, if the address we wanna write to isnt on the cartridge, we return false and we write to the bus normally
        return;
    }else{
        if(PPU_START <= address && address <= PPU_END){
            address = TRANSLATE_PPU_ADDRESS(address);
            ppuRegWrite(address, data & 0xFF);
            return;
        }

        if(address == JOYPAD1_ADDR){
            if((data & 0b1) == 0b1){
                joypad_prepare_read();
            }else if((data & 0b1) == 0b0){
                joypad_publish_state();
            }
            return;
        }
        if(address <= 0x1FFF){ //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address &= 0b11111111111;
            bus[address] = (byte)data;
            return;
        }

        if(address == OAM_DMA_ADDR){
            //STOP!! TRIGGER DIRECT MEMORY ACCESS: copy one entire page of cpu memory to the PPU's OAM (sprite state sheet), as manually doing this usind PPU oam data/addr regs would be slow. Almost all roms but the most primitive use this.

            performOamDMA((byte)data); //because DMAs *usually* happen during vblank, we can get away with an instant copy and just bill the cycle cost to the cpu artificially, but to be 100% accurate we could paint it in as one by one, but that seems useless for the most part

            cpu_timeout_cycles += 514; //bill it to the cpu

            return;

        }

        DWARN("Could not write to address 0x%X! Open Bus Write!", address);
    }
}

byte busRead8(word address){

    static byte data = 0;
    //we first ask the mapper to read the data from the address for us in case its on the cartridge, if it returns false that means the data stored at that address is not on the cartridge, but rather on the nes memory, thus we hand the job over to the bus
    bool valid_cart_response = cart_PRG_Read(address, &data);
    if(valid_cart_response){
        return data;
    }else{


        if(address <= 0x1FFF){ //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address &= 0b11111111111;
            data = bus[address];
            return data;
        }


        if(PPU_START <= address && address <= PPU_END){
            address = TRANSLATE_PPU_ADDRESS(address);
            data = ppuRegRead(address);
            return data;
        }

        if(address == JOYPAD1_ADDR){
            byte joypad_read = joypad_read_bit(JOYPAD_1);
            byte joypad_mask = 0b1111; //Only affects lower 4 bits
            data = (data & ~joypad_mask) | (joypad_read & joypad_mask); //Keep the upper 4 bits of last read byte

            return data;
        }

        DWARN("Open bus read 0x%X", address);
        return data;
    }
}

word busRead16(word address){
    word d = busRead8(address+1); //read msb
    d <<= 8; //put msb in the msb section
    d |= busRead8(address); //read lsb
    return d; //return whole word
}

void busWrite16(word address, word data){
    busWrite8(address, data >> 8); //write msb
    busWrite8(address + 1, data & 0b00001111); //write lsb
}

void dumpBus(){
    FILE *fdump;
    fdump = fopen("dumpfile", "w");

    SMART_ASSERT(fdump != NULL, "ERR: Could not access file! Do you have permissions to create a file?\n");

    //If the file was successfuly opened then this code will run
    for(unsigned long long i = 0; i <= 0xFFFF; i++)
        fprintf(fdump, "%c", busRead8(i));
}

byte debug_read_do_not_use_pls(word address){
    return bus[address];
}


static inline void debug_print_instruction(CPU* __restrict__ cpu, byte opcode){
    handleErrors(cpu);
    return;
    #ifdef DEBUG
        printf("\n--name: %s opcode: %02X address: %04X    %d %p\n",
            cpu->opcodes[opcode].name,
            opcode,
            cpu->PC,
            cpu->opcodes[opcode].bytes,
            cpu->opcodes[opcode].microcode
        );
    #endif
}







#define ROM_TEST_NAME ("nestest.nes")


int main(int argc, char * argv[]){
    CPU * cpu = (CPU*)xmalloc(sizeof(CPU)); //create new CPU

    initCpu(cpu); //put new CPU in starting mode and dock it to the bus

    if(argc <= 1){ //Check to see if a rom was given
        PRINT_ERROR("rom", "No Rom file Specified in Arguments");
        exit(EXIT_FAILURE);
    }

    //load the CHR and PRG banks from the .nes file (argv[1]), also loads the header for mapper construction
    initBanks(argv[1]);
    if(strstr(argv[1], ROM_TEST_NAME)){
        if(strlen(strstr(argv[1], ROM_TEST_NAME)) == strlen(ROM_TEST_NAME)){ //Load test rom at 0xC000
            cpu->PC = 0xC000;
        }else{
            cpu->PC = romStartAddress; //Rom was in a folder called 'ROM_TEST_NAME' rather than loading a file with the same name
        }
    }else{
        cpu->PC = romStartAddress; //Rom was not a test rom. Load normally
    }

    initPpu(); //Create ppu thread and initalize memory

    while(true){

        #ifdef DEBUG
            #if 0
                debug_print_instruction(cpu, busRead8(cpu->PC));
                printRegisters(cpu);
                printCpu(cpu);
            #endif
        #endif

        //
        //
        //RUN THE CPU CLOCK ONE TIME

        int cpuCycles = cpuClock(cpu);
        cpuCycles += cpu_timeout_cycles;
        cpu_timeout_cycles = 0;

        #ifdef DEBUG
            debug_print_instruction(cpu, busRead8(cpu->PC));
        #endif

        for(int i = 0; i < 3 * cpuCycles; ++i)
            ppuClock(cpu);

        //
        //
        //


        #ifdef DEBUG
            #ifdef TICKONKEY
                getchar();
            #endif
        #endif

        if(window_shutdown_triggered()){
            cpuDestroy(cpu);
            free(cpu);

            window_destroy();
            return EXIT_SUCCESS;
        }
    }

    #ifdef DEBUG
        dumpBus();
    #endif
}
