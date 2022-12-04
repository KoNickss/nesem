#include "common.h"

//Edit these defines to determine the debug outputs

//#define DEBUG //General debug things such as running out of memory
//#define DISABLE_LDA_DEBUG //LDA had a error in development and this flag is used to disable error reporting from LDA

//

#define STACK_RAM_OFFSET 0x100

typedef union{
    struct var_t{
        byte Carry : 1;
        byte Zero : 1;
        byte Interrupt : 1;
        byte Decimal : 1;
        byte Break : 1;
        byte ignored : 1;
        byte Overflow : 1;
        byte Negative : 1;
    }flags;

    byte data;
}SR_t; 


//this is a union, 'flags' (the one with individual bits) and 'data' are synced, to write a byte to the SR write to data, to flip a individual bit write to flags
// aka to set negative to false you do "cpu.SR.flags.Negative = 0;", to write a whole byte to the SR, where each bit of the byte represents a flip bit (negative, overflow, etc) do "cpu.SR.data = byte_data;"

typedef struct{
    word address;
    byte value;
} busTransaction; //this is the type we use for our addressing types, they resolve to this type which contains a value and address


struct instruction;

typedef struct CPU{
    word PC; //Program Counter
    byte A; //Accumulator
    byte X; //X reg
    byte Y; //Y reg
    SR_t SR; //Stack register
    byte SP; //Stack pointer

    struct instruction * opcodes; //opcodes struct;

    //not part of the ACTUAL cpu, but useful flags I use for emulation
    bool pcNeedsInc; //checks if CPU needs its PC incremented by the number of bytes of the opcode (all opcodes except the sub-routine ones, look for the flag in the code)
}CPU;

struct instruction{
    void (*microcode)(struct CPU *, word bytes, busTransaction (*addressing)(CPU *, word)); //generic definition for opcode funcs such as BRK, ORA, etc
    busTransaction (*mode)(struct CPU * cpu, word bytes); //check line 85
    char * name;
    char bytes;
    char cycles;
};

void printCpu(CPU * cpu); //debug functions
void raiseError(unsigned int err, CPU * cpu);
void handleErrors(CPU * cpu);
void printRegisters(CPU * cpu);


void initCpu(CPU * cpu); //init cpu, allocate memory for opcode register and set flags to initial state
void cpuNmi(CPU * cpu); //process non-maskable interrupt, do not use this to trigger nmi, run the activateCpuNmi function in the bus instead

void cpuClock(CPU * cpu); //tick function