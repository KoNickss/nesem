#include "bus.h"
#include <string.h>

//For displaying to the screen
#include "window.h"

bool debug = true;


unsigned char bus[BUS_SIZE]; //this is the bus array

//Importing the different electrical components of the NES as definitions, not yet docked to the bus

#include "cpu.h"

#include "ppu.h"

#include "cartridge.h"

#include "controller.h"

#define TRANSLATE_PPU_ADDRESS(address) ((address - 0x2000) % 8 + 0x2000)

void busWrite8(word address, word data){
    /*if(address == 0x4016){
        if((data & 0b1) == 0b1){
		    joypad_prepare_read();
        }else if((data & 0b1) == 0b0){
            joypad_publish_state();
        }
	}*/
    if(!mapper000_Write(address, data, false)){ //first thing we do is we hand the operation to the mapper to resolve any cartridge-side bank switching and mirroring, if the address we wanna write to isnt on the cartridge, we return false and we write to the bus normally
        
        if(address <= 0x1FFF) //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address %= 0x07FF;


        if(0x2000 <= address && address <= 0x3FFF){
            address = TRANSLATE_PPU_ADDRESS(address);
            ppuRegWrite(address, data & 0xFF);
            return;
        }

        bus[address] = (unsigned char)data;
    }
}

word busRead8(word address){
	if(address == 0x4016){
		return joypad_read_bit(JOYPAD_1);
	}
    word data;
    if((data = mapper000_Read(address, false)) >= 0x100){ //we first ask the mapper to read the data from the address for us in case its on the cartridge, if it returns 0x100 (0xFF + 1 aka impossible to get from reading a byte) that means the data stored at that address is not on the cartridge, but rather on the nes memory, thus we hand the job over to the bus
        
        
        if(address <= 0x1FFF) //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address %= 0x07FF;


        if(0x2000 <= address && address <= 0x3FFF){
            address = TRANSLATE_PPU_ADDRESS(address);
            return ppuRegRead(address);
        }


        return bus[address];
    }else{
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
    
    #ifdef DEBUG
        if(fdump == NULL){
            fprintf(stderr, "ERR: Could not access file! Do I have permissions to create a file?\n");
            exit(EXIT_FAILURE);
        }
    #endif
    
    //If the file was successfuly opened then this code will run
    for(unsigned long long i = 0; i <= 0xFFFF; i++) 
        fprintf(fdump, "%c", busRead8(i));
}

byte debug_read_do_not_use_pls(word address){
    return bus[address];
}


static inline void debug_print_instruction(CPU* __restrict__ cpu, byte opcode){
    handleErrors(cpu);
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
    CPU * cpu = (CPU*)malloc(sizeof(CPU)); //create new CPU

    initCpu(cpu); //put new CPU in starting mode and dock it to the bus

    initPpu(); //Create ppu thread and initalize memory

    if(argc <= 1){ //Check to see if a rom was given
        fprintf(stderr, "ERR: No Rom file Specified in Arguments\n");
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

    while(true){

        #ifdef DEBUG
            #if 0
                debug_print_instruction(cpu, busRead8(cpu->PC));
                printRegisters(cpu);
                printCpu(cpu);    
            #endif  
        #endif

        if(ppuGetNmiStatus()){
            ppuClearNmiStatus();
            cpuNmi(cpu);
        }

        //
        //
        //RUN THE CPU CLOCK ONE TIME
        cpuClock(cpu);
        debug_print_instruction(cpu, busRead8(cpu->PC));
        ppuClock();
        ppuClock();
        ppuClock();
        //
        //
        //


        #ifdef DEBUG
            #ifdef TICKONKEY
                getchar();
            #endif
        #endif
    }
    
    #ifdef DEBUG
        dumpBus();
    #endif
}
