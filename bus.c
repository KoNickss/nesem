#include "bus.h"

bool debug = true;


unsigned char bus[BUS_SIZE]; //this is the bus array

//Importing the different electrical components of the NES as definitions, not yet docked to the bus

#include "cpu.h"

#include "cartridge.h"


word mapper_resolve_read(word address){ //info about what regions are mirrored can be found on the nes dev wiki

    if(address <= 0x1FFF && address >= 0x0800)
        address %= 0x0800;

    if(address <= 0x3FFF && address >= 0x2008)
        address = ((address - 0x2000) % 0x8) + 0x2000;
            
    return address;
}

word mapper_resolve_write(word address, byte data){ //info about what regions are mirrored can be found on the nes dev wiki

    if(address <= 0x1FFF && address >= 0x0800)
        address %= 0x0800;

    if(address <= 0x3FFF && address >= 0x2008)
        address = ((address - 0x2000) % 0x8) + 0x2000;
            
    return address;
}

void bus_write8(word address, word data){
    if(!mapper000_write(address, data, false)){ //first thing we do is we hand the operation to the mapper to resolve any cartridge-side bank switching and mirroring, if the address we wanna write to isnt on the cartridge, we return false and we write to the bus normally
        address = mapper_resolve_write(address, data); //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
        
        #ifdef DEBUG
            printf("\nwrite-ram 0x%04X at 0x%04X\n; old_val 0x%04X", data, address, bus_read8(address));
        #endif

        bus[address] = (unsigned char)data;
    }
}

word bus_read8(word address){
    word data;
    if((data = mapper000_read(address, false)) >= 0x100){ //we first ask the mapper to read the data from the address for us in case its on the cartridge, if it returns 0x100 (0xFF + 1 aka impossible to get from reading a byte) that means the data stored at that address is not on the cartridge, but rather on the nes memory, thus we hand the job over to the bus
        address = mapper_resolve_read(address); //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
        return bus[address];
    }else{
        return data;
    }
}

word bus_read16(word address){
    word d = bus_read8(address); //read msb
    d <<= 8; //put msb in the msb section
    d |= bus_read8(address+1); //read lsb
    return d; //return whole word
}

void bus_write16(word address, word data){
    bus_write8(address, data >> 8); //write msb
    bus_write8(address + 1, data & 0b00001111); //write lsb
}

void dump_bus(){
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
        fprintf(fdump, "%c", bus_read8(i));
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


#define PROG_START_ADDR 0xC000

int main(int argc, char * argv[]){
    CPU * cpu = (CPU*)malloc(sizeof(CPU)); //create new CPU
    #ifdef DEBUG
        if(cpu == NULL){
            fprintf(stderr, "ERR: Out of RAM!\n");
            exit(EXIT_FAILURE);
        }
    #endif

    init_cpu(cpu); //put new CPU in starting mode and dock it to the bus
    cpu->PC = PROG_START_ADDR;

    if(argc <= 1){ //Check to see if a rom was given
        fprintf(stderr, "ERR: No Rom file Specified in Arguments\n");
        exit(EXIT_FAILURE);
    }

    //load the CHR and PRG banks from the .nes file (argv[1]), also loads the header for mapper construction
    init_banks(argv[1]);

    #ifdef DEBUG
        //Test to see if writing to the bus is working correctly
        bus_write8(0x0000, 0xFF);
        printf("\n---- ---- %02X %02X\n", bus_read8(0x0000), bus[0x0000]);


        //open debug PC logfile
        FILE * PClogFILE;
        PClogFILE = fopen("PClogFILE", "w");
    #endif

    for(long iterations = 0; iterations != 3500; iterations++){

        #ifdef DEBUG
            fprintf(PClogFILE, "%4X\n", cpu->PC);
            debug_print_instruction(cpu, bus_read8(cpu->PC));
            print_registers(cpu);
            print_cpu(cpu);      
            fprintf(stderr, "\n\n----\n%i\n-----", iterations);
        #endif
        
        //RUN THE CPU CLOCK ONE TIME
        cpu_clock(cpu);
    }
    
    #ifdef DEBUG
        dump_bus();
    #endif
}
