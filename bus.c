#include "bus.h"

bool debug = true;


unsigned char bus[BUS_SIZE]; //this is the bus array

//Importing the different electrical components of the NES as definitions, not yet docked to the bus

#include "cpu.h"

#include "ppu.h"

#include "cartridge.h"

bool activateCpuNmiBool = false;


void busWrite8(word address, word data){
    if(!mapper000_Write(address, data, false)){ //first thing we do is we hand the operation to the mapper to resolve any cartridge-side bank switching and mirroring, if the address we wanna write to isnt on the cartridge, we return false and we write to the bus normally
        
        if(0x0000 <= address && address <= 0x1FFF) //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address %= 0x07FF;


        if(0x2000 <= address && address <= 0x3FFF){
            //ppu stuff in the future
            address = (address - 0x2000) % 8 + 0x2000;
        }

        #ifdef DEBUG
            printf("\nwrite-ram at 0x%04X val = 0x%04X\n; old_val 0x%04X", address, data, busRead8(address));
        #endif

        bus[address] = (unsigned char)data;
    }
}

word busRead8(word address){
    word data;
    if((data = mapper000_Read(address, false)) >= 0x100){ //we first ask the mapper to read the data from the address for us in case its on the cartridge, if it returns 0x100 (0xFF + 1 aka impossible to get from reading a byte) that means the data stored at that address is not on the cartridge, but rather on the nes memory, thus we hand the job over to the bus
        
        
        if(0x0000 <= address && address <= 0x1FFF) //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address %= 0x07FF;


        if(0x2000 <= address && address <= 0x3FFF){
            //ppu stuff in the future
            address = (address - 0x2000) % 8 + 0x2000;
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
    printf("\n--name: %s opcode: %02X address: %04X    %d %p\n", 
        cpu->opcodes[opcode].name, 
        opcode, 
        cpu->PC,  
        cpu->opcodes[opcode].bytes, 
        &cpu->opcodes[opcode].microcode
    );
}


void activateCpuNmi(){
    activateCpuNmiBool = true;
}



#define PROG_START_ADDR 0xC000

int main(int argc, char * argv[]){

    activateCpuNmiBool = false;

    CPU * cpu = (CPU*)malloc(sizeof(CPU)); //create new CPU


    #ifdef DEBUG
        if(cpu == NULL){
            fprintf(stderr, "ERR: Out of RAM!\n");
            exit(EXIT_FAILURE);
        }
    #endif

    initCpu(cpu); //put new CPU in starting mode and dock it to the bus
    cpu->PC = PROG_START_ADDR;

    if(argc <= 1){ //Check to see if a rom was given
        fprintf(stderr, "ERR: No Rom file Specified in Arguments\n");
        exit(EXIT_FAILURE);
    }

    //load the CHR and PRG banks from the .nes file (argv[1]), also loads the header for mapper construction
    initBanks(argv[1]);

    #ifdef DEBUG
        //Test to see if writing to the bus is working correctly
        busWrite8(0x0000, 0xFF);
        printf("\n---- ---- %02X %02X\n", busRead8(0x0000), bus[0x0000]);


        //open debug PC logfile
        FILE * PClogFILE;
        PClogFILE = fopen("PClogFILE", "w");
    #endif

    #ifdef DEBUG
        //ppu tests
        printf("\nCACA\n");
        printf("\nt\n");
        initPpu();
        printf("\nk\n");
        ppuRegWrite(0x2006, 0x00);
        printf("\nok\n");
        ppuRegWrite(0x2006, 0x10);
        printf("\nok1\n");
        ppuRegWrite(0x2007, 0x22);
        printf("\nok2\n");
        ppuRegWrite(0x2007, 0x33);
        printf("\nok3\n");
        dumpPpuBus();
    #endif


    for(long iterations = 0; iterations != 9000; iterations++){

        #ifdef DEBUG
            fprintf(PClogFILE, "%4X\n", cpu->PC);
            debug_print_instruction(cpu, busRead8(cpu->PC));
            printRegisters(cpu);
            printCpu(cpu);      
            fprintf(stderr, "\n\n----\n%i\n-----", iterations);
        #endif
        
        if(activateCpuNmiBool){
            cpuNmi(cpu);
            activateCpuNmiBool = false;
        }
        //RUN THE CPU CLOCK ONE TIME
        cpuClock(cpu);
    }
    
    #ifdef DEBUG
        dumpBus();
    #endif
}
