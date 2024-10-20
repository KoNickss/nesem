#include "cpu.h" //plsease read the header file before reading this file
#include "bus.h" //includes bus-wide definitions such as bus write/read functions and data types generic among all nes components


//Addressing modes

//Quick summary: I made a function pointer to one of these functions defined in init_opcodereg() as .mode, so when calling the function
//"addressing" from any opcode func, it returns a busTransaction type containing a value and adress (of that value on the bus), this is so that we
//dont have to resolve manually for each adressing mode of the opcode, we just call adressing and it points to one of the functions below,
//that resolves the adress for us and leaves us with a generic transaction type that we can then generically manipulate in each opcode func in a way that
//works with all addressing modes without having to branch code for each individual addressing type, exceptions include opcodes that may write to the accumulator,
//since the accumulator isn't on the bus, we need to (sadly) branch the code, we could mirror the accumulator to unused address on the bus, but thats overly-complicated
//and branching is just easier

//Quick tip: bytes is the arguments invoked, so for 0xFF 0xA0 0xE2 where 0xFF is the instruction, bytes is 0xA0 0xE2


//REMOVE AFTER DEBUGGING DONE

void print_stack(CPU * cpu);
//////////////////////////////////


//Gets a new PC to travel to for IRQ, NMI, and RESET
static inline word decodeRomPCVector(word addr){
	if(addr >= 0xFFFF || addr < 0xFFFA){
		fprintf(stderr, "[%s] Invalid address of 0x%X!\n", __FUNCTION__, addr);
		abort();
	}
	word ret = ((word)busRead8(addr + 1)) << 8;
	ret |= busRead8(addr);
	return ret;
}


busTransaction IMM(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    x.address = 0; //q: why do we set the address to 0 when its immediate (aka presented as an as-is argument without being tied to any bus address)? because any function that writes to its busTransaction types lacks an IMM mode bcuz you can't write data to nothingness (duh)
    x.value = bytes;
    return x;
}


busTransaction IMP(CPU * __restrict__ cpu, word bytes){
    busTransaction x; //implied opcodes are opcodes that require no arguments, but to keep things uniform we made this too, IMP opcodes dont even use this at all so idk
    return x;
}

busTransaction ACC(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    x.address = 0; //as explained above, the accumulator isnt on the bus, so we just branch manually for the opcodes that do this and write directly to the acc, thus the address goes unused
    x.value = cpu->A;
    return x;
}

busTransaction ZPG(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    x.address = bytes;
    x.value = busRead8(bytes);
    return x;
}

busTransaction ZPGX(CPU * __restrict__ cpu, word bytes){ //not code related, but as a fun fact of the day, addr modes that get offsetted by X or Y were used to create simple arrays, kinda cool
    busTransaction x;
    bytes += cpu->X;
    x.address = bytes & 0xFF;
    x.value = busRead8(x.address);
    return x;
}

busTransaction ZPGY(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    bytes += cpu->Y;
    x.address = bytes & 0xFF;
    x.value = busRead8(x.address);
    return x;
}

busTransaction REL(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    byte offset = (byte)bytes;
    x.address = cpu->PC + *(char*)&offset; //typecast offset to convert it into a signed number
    x.value = busRead8(x.address);
    return x;
}

busTransaction ABS(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    x.address = bytes;
    x.value = busRead8(bytes);
    return x;
}

busTransaction ABSX(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    bytes += cpu->X;
    x.address = bytes;
    x.value = busRead8(bytes);
    return x;
}

busTransaction ABSY(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    bytes += cpu->Y;
    x.address = bytes;
    x.value = busRead8(bytes);
    return x;
}

busTransaction IND(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    x.address = busRead16(bytes);

    //BAD CODE INCOMING:
    //ok so there is an exception JMP(6C) makes that, from the way I
    //wrote this code architecturally, I can't solve in an organic way
    //other than making an exception right here, in the IND addresing mode
    //that is used exclusively by JMP(6C) and nothing else, this sucks I know
    //I'll try to find a smoother fix in the near future.

    //The exception being that, when reading the value stored in bytes, it doesn't
    //use carry, so for example for 0x02FF it doesnt read the 16 bit value in (0x02FF and 0x300),
    //but rather (0x02FF and 0x0200), on the same page, since it cant carry over to the next page
    //do not ask how long it took to find info about this, I feel this shouldn't really be
    //brushed over so carelessly

    //Signed, Joaquin

    if((bytes & 0x00FF) == 0xFF){
        x.address = busRead8(bytes) | (busRead8(bytes - 0xFF) << 8);
    }

    x.value = busRead8(x.address);
    return x;
}

busTransaction INDX(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    
    x.address = busRead8((bytes + cpu->X) % 256) + busRead8((bytes + cpu->X + 1) % 256) * 256;
    x.value = busRead8(x.address);

    return x;
}

busTransaction INDY(CPU * __restrict__ cpu, word bytes){
    busTransaction x;
    
    x.address = busRead8(bytes) + busRead8((bytes + 1) % 256) * 256 + cpu->Y;
    x.value = busRead8(x.address);

    return x;
}





//Microcode Instructions






void ORA(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word)){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->A |= x.value;
    cpu->SR.flags.Zero = !cpu->A;
    cpu->SR.flags.Negative = cpu->A >> 7;
}

void ASL(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word)){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details

    cpu->SR.flags.Carry = x.value >> 7;
    x.value <<= 1;

    if(addressing == &ACC)
        cpu->A = x.value;
    else
        busWrite8(x.address, x.value);

    cpu->SR.flags.Zero = !x.value;
    cpu->SR.flags.Negative = x.value >> 7;
}

void AND(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details

    cpu->A &= x.value;
    cpu->SR.flags.Zero = !cpu->A;
    cpu->SR.flags.Negative = cpu->A >> 7;
}

void BRK(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){ //0x00 Hardware interupt.
	cpu->pcNeedsInc = false;
	cpu->PC += 2;
	cpu->SR.flags.Interrupt = true;

	byte pcLsb, pcMsb;
	pcMsb = cpu->PC >> 8;
	pcLsb = cpu->PC & 0x00FF;

	busWrite8(cpu->SP + STACK_RAM_OFFSET, pcMsb);
	cpu->SP--;
	busWrite8(cpu->SP + STACK_RAM_OFFSET, pcLsb);
	cpu->SP--;

	cpu->SR.flags.Break = 1;
	busWrite8(cpu->SP + STACK_RAM_OFFSET, cpu->SR.data);
	cpu->SP--;
	cpu->SR.flags.Break = 0;

	cpu->PC = decodeRomPCVector(ROM_VECTOR_IRQ);
}

void PHP(CPU* cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){ //0x08 PHP Push Status register to the stack

    cpu->SR.flags.Break = 1;
    cpu->SR.flags.ignored = 1;
    
    busWrite8(cpu->SP + STACK_RAM_OFFSET, cpu->SR.data);
    cpu->SP--;

    cpu->SR.flags.Break = 0;
}


void BPL(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(!cpu->SR.flags.Negative) cpu->PC = x.address;
}

void CLC(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SR.flags.Carry = 0;
}

void JSR(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){ //JSR - Jump to new absolute address
    busTransaction x = addressing(cpu, bytes);
    
    cpu->pcNeedsInc = false;
    cpu->PC += 2;
    byte pcLsb, pcMsb;
    pcMsb = cpu->PC >> 8;
    pcLsb = cpu->PC & 0x00FF;

    busWrite8(cpu->SP + STACK_RAM_OFFSET, pcMsb);
    cpu->SP--;
    busWrite8(cpu->SP + STACK_RAM_OFFSET, pcLsb);
    cpu->SP--;

    cpu->PC = x.address;
}

void BIT(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    cpu->SR.flags.Zero = !(cpu->A & x.value);
    cpu->SR.flags.Negative = (x.value >> 7) & 1;
    cpu->SR.flags.Overflow = (x.value >> 6) & 1;
}

void PLP(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SP++;
    cpu->SR.data = busRead8(cpu->SP + STACK_RAM_OFFSET);
    cpu->SR.flags.Break = 0;
    cpu->SR.flags.ignored = 1;
}

void PLA(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SP++;
    cpu->A = busRead8(cpu->SP + STACK_RAM_OFFSET);
    cpu->SR.flags.Negative = cpu->A > 7;
    cpu->SR.flags.Zero = !cpu->A;
}

void ROL(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    bool new_carry = x.value >> 7;
    x.value <<= 1;
    x.value |= cpu->SR.flags.Carry;
    cpu->SR.flags.Zero = !x.value;
    cpu->SR.flags.Negative = x.value >> 7;

    if(addressing == &ACC)
        cpu->A = x.value;

    else busWrite8(x.address, x.value);

    cpu->SR.flags.Carry = new_carry;
}

void BMI(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(cpu->SR.flags.Negative) cpu->PC = x.address;
}

void ADC(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word)){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details

    int tmp = cpu->A + x.value + cpu->SR.flags.Carry;
    bool sign = (tmp >> 7) & 1;
    cpu->SR.flags.Overflow = ((cpu->A >> 7) != sign) && ((x.value >> 7) != sign);

    cpu->SR.flags.Carry = tmp > 255;
    cpu->SR.flags.Zero = !((byte)tmp);
    cpu->SR.flags.Negative = sign; //check for bit 7

    cpu->A = (byte)tmp;
}

void BCC(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(!cpu->SR.flags.Carry) cpu->PC = x.address;
}

void BCS(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(cpu->SR.flags.Carry) cpu->PC = x.address;
}

void BEQ(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(cpu->SR.flags.Zero) cpu->PC = x.address;
}

void ROR(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    bool oldCarry = cpu->SR.flags.Carry;
    cpu->SR.flags.Carry = x.value & 0b00000001;
    x.value >>= 1;

    #ifdef USE_OLD_ROR
        x.value |= cpu->SR.flags.Carry;
        cpu->SR.flags.Zero = !x.value;
        cpu->SR.flags.Negative = x.value;
    #else
        x.value |= ((word)oldCarry) << 7;
        cpu->SR.flags.Zero = (x.value == 0);
        cpu->SR.flags.Negative = ((x.value & 0b10000000) != 0);
    #endif

    if(addressing == &ACC)
        cpu->A = x.value;

    else busWrite8(x.address, x.value);
}

void CLD(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SR.flags.Decimal = 0;
}

void EOR(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->A ^= x.value;
    cpu->SR.flags.Zero = !cpu->A;
    cpu->SR.flags.Negative = cpu->A >> 7;
}

void BNE(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(!cpu->SR.flags.Zero) cpu->PC = x.address;
}

void BVC(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(!cpu->SR.flags.Overflow) cpu->PC = x.address;
}

void BVS(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes);
    if(cpu->SR.flags.Overflow) cpu->PC = x.address;
}

void CLI(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SR.flags.Interrupt = 0;
}

void CLV(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SR.flags.Overflow = 0;
}

void CMP(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->SR.flags.Negative = (cpu->A - x.value) >> 7;

    cpu->SR.flags.Carry = (cpu->A >= x.value);
    cpu->SR.flags.Zero = (cpu->A == x.value);
}

void CPX(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->SR.flags.Negative = (cpu->X - x.value) >> 7;


    cpu->SR.flags.Carry = (cpu->X >= x.value);
    cpu->SR.flags.Zero = (cpu->X == x.value);
    
    //TODO: Double check if CPX sets the same flags as CMP
    /*if(cpu->X < x.value){
        cpu->SR.flags.Carry = 0;
        cpu->SR.flags.Zero = 0;
    }

    if(cpu->X == x.value){
        cpu->SR.flags.Carry = 1;
        cpu->SR.flags.Zero = 1;
    }

    if(cpu->X > x.value){
        cpu->SR.flags.Carry = 1;
        cpu->SR.flags.Zero = 0;
    }*/
}

void CPY(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->SR.flags.Negative = (cpu->Y - x.value) >> 7;

    cpu->SR.flags.Carry = (cpu->Y >= x.value);
    cpu->SR.flags.Zero = (cpu->Y == x.value);
    //TODO: See if CPY sets the same flags as CMP
    /*
    if(cpu->Y < x.value){
        cpu->SR.flags.Carry = 0;
        cpu->SR.flags.Zero = 0;
    }

    if(cpu->Y == x.value){
        cpu->SR.flags.Carry = 1;
        cpu->SR.flags.Zero = 1;
    }

    if(cpu->Y > x.value){
        cpu->SR.flags.Carry = 1;
        cpu->SR.flags.Zero = 0;
    }*/
}

void DEC(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    x.value--;
    //TODO: Does this need an ACC option?
    busWrite8(x.address, x.value);
    cpu->SR.flags.Negative = (x.value >> 7);
    cpu->SR.flags.Zero = (x.value == 0);
}

void DEX(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->X -= 1;
    cpu->SR.flags.Negative = (cpu->X >> 7);
    cpu->SR.flags.Zero = (cpu->X == 0);
}

void DEY(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->Y -= 1;
    cpu->SR.flags.Negative = (cpu->Y >> 7);
    cpu->SR.flags.Zero = (cpu->Y == 0);
}

void INC(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    x.value++;
    busWrite8(x.address, x.value);
    cpu->SR.flags.Zero = !x.value;
    cpu->SR.flags.Negative = x.value >> 7;
}

void INX(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->X += 1;
    cpu->SR.flags.Negative = (cpu->X >> 7);
    cpu->SR.flags.Zero = (cpu->X == 0);
}

void INY(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->Y += 1;
    cpu->SR.flags.Negative = (cpu->Y >> 7);
    cpu->SR.flags.Zero = (cpu->Y == 0);
}

void JMP(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->PC = x.address;
    cpu->pcNeedsInc = false;

    //Part of the code for this opcode that manages the scenario in which we read from 0xXXFF
    //(scenario in which we read the msb from 0xXX00, not 0xXY00, as would be logical, because
    //JMP doesnt use carry and as such can't read from the next page) is regrettably written directly
    //in the IND function that is only used by JMP(6C) because including it here would've overly complicated
    //for no reason, look at the IND function for further detailing
}

void LDA(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details    
    cpu->A = x.value;
    cpu->SR.flags.Zero = (cpu->A == 0);
    cpu->SR.flags.Negative = cpu->A >> 7;
}

void LDX(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->X = x.value;
    cpu->SR.flags.Zero = !x.value;
    cpu->SR.flags.Negative = x.value >> 7;
}

void LDY(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    cpu->Y = x.value;
    cpu->SR.flags.Zero = !x.value;
    cpu->SR.flags.Negative = x.value >> 7;
}

void LSR(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    byte new_val = (x.value >> 1);

    if(addressing == &ACC){
        cpu->A = new_val;
    } else {
        busWrite8(x.address, new_val);
    }

    cpu->SR.flags.Carry = (x.value & 0b00000001);
    cpu->SR.flags.Negative = 0;
    cpu->SR.flags.Zero = (new_val == 0);
}

void NOP(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    //nothing to see here
}

void PHA(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busWrite8(cpu->SP + STACK_RAM_OFFSET, cpu->A);
    cpu->SP--;
}

void RTI(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->pcNeedsInc = false;
    cpu->SP++;
    cpu->SR.data = busRead8(cpu->SP + STACK_RAM_OFFSET);

    cpu->SR.flags.ignored = 1;

    cpu->SP++;
    word newPC = busRead8(cpu->SP + STACK_RAM_OFFSET); //read lsb
    cpu->SP++;
    newPC |= busRead8(cpu->SP + STACK_RAM_OFFSET) << 8; //read msb

    cpu->PC = newPC;
}

void RTS(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SP++;
    word newPC = busRead8(cpu->SP + STACK_RAM_OFFSET);

    cpu->SP++;
    newPC |= busRead8(cpu->SP + STACK_RAM_OFFSET) << 8;

    cpu->PC = newPC;
}

void SBC(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word)){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details

    x.value ^= 0x00FF;

    //now run ADC code but with the arg inverted, somehow this works

    int tmp = cpu->A + x.value + cpu->SR.flags.Carry;
    bool sign = ((tmp >> 7) & 1);
    cpu->SR.flags.Overflow = ((cpu->A >> 7) != sign) && ((x.value >> 7) != sign);

    cpu->SR.flags.Carry = tmp > 255;
    cpu->SR.flags.Zero = !((byte)tmp);
    cpu->SR.flags.Negative = sign; //check for bit 7

    cpu->A = (byte)tmp;
}

void SEC(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word)){
    cpu->SR.flags.Carry = 1;
}

void SEI(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word)){
    cpu->SR.flags.Interrupt = 1;
}

void SED(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SR.flags.Decimal = 1;
}

void STA(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    busWrite8(x.address, cpu->A);
}

void STX(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    busWrite8(x.address, cpu->X);
}

void STY(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    busTransaction x = addressing(cpu, bytes); //check line 85 for details
    busWrite8(x.address, cpu->Y);
}

void TAX(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->X = cpu->A;
    cpu->SR.flags.Zero = !cpu->X;
    cpu->SR.flags.Negative = cpu->X >> 7;
}

void TAY(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->Y = cpu->A;
    cpu->SR.flags.Zero = !cpu->Y;
    cpu->SR.flags.Negative = cpu->Y >> 7;
}

void TSX(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->X = cpu->SP;
    cpu->SR.flags.Zero = !cpu->X;
    cpu->SR.flags.Negative = cpu->X >> 7;
}

void TXA(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->A = cpu->X;
    cpu->SR.flags.Zero = !cpu->A;
    cpu->SR.flags.Negative = cpu->A >> 7;
}

void TXS(CPU * __restrict__ cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->SP = cpu->X;
}

void TYA(CPU * cpu, word bytes, busTransaction (*addressing)(CPU *, word) ){
    cpu->A = cpu->Y;
    cpu->SR.flags.Zero = !cpu->A;
    cpu->SR.flags.Negative = cpu->A >> 7;
}

void initOpcodeReg(CPU * cpu){ //opcode code defined starting line 139

    cpu->opcodes = (struct instruction*)malloc(sizeof(struct instruction) * 0xFF); //allow memory for opcode array in cpu_opcodereg.h
    if(cpu->opcodes == NULL){
        fprintf(stderr, "ERR: Out of Memory\n");
        exit(EXIT_FAILURE);
    }

    //BRK codes

    cpu->opcodes[0x00].microcode = &BRK;
    cpu->opcodes[0x00].mode = &IMP;
    cpu->opcodes[0x00].name = "Force Break";
    cpu->opcodes[0x00].cycles = 7;
    cpu->opcodes[0x00].bytes = 1;

    //ORA codes

    cpu->opcodes[0x01].microcode = &ORA;
    cpu->opcodes[0x01].mode = &INDX;
    cpu->opcodes[0x01].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x01].cycles = 6;
    cpu->opcodes[0x01].bytes = 2;

    cpu->opcodes[0x05].microcode = &ORA;
    cpu->opcodes[0x05].mode = &ZPG;
    cpu->opcodes[0x05].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x05].cycles = 3;
    cpu->opcodes[0x05].bytes = 2;

    cpu->opcodes[0x09].microcode = &ORA;
    cpu->opcodes[0x09].mode = &IMM;
    cpu->opcodes[0x09].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x09].cycles = 2;
    cpu->opcodes[0x09].bytes = 2;

    cpu->opcodes[0x0D].microcode = &ORA;
    cpu->opcodes[0x0D].mode = &ABS;
    cpu->opcodes[0x0D].name = "OR Accumulator with immidate";
    cpu->opcodes[0x0D].cycles = 4;
    cpu->opcodes[0x0D].bytes = 3;

    cpu->opcodes[0x11].microcode = ORA;
    cpu->opcodes[0x11].mode = &INDY;
    cpu->opcodes[0x11].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x11].cycles = 5;
    cpu->opcodes[0x11].bytes = 2;

    cpu->opcodes[0x15].microcode = &ORA;
    cpu->opcodes[0x15].mode = &ZPGX;
    cpu->opcodes[0x15].name = "OR with zero page memory w/ Accumulator";
    cpu->opcodes[0x15].cycles = 4;
    cpu->opcodes[0x15].bytes = 2;

    cpu->opcodes[0x1D].microcode = &ORA;
    cpu->opcodes[0x1D].mode = &ABSX;
    cpu->opcodes[0x1D].name = "OR Absolute Mem + X w/Accumulator";
    cpu->opcodes[0x1D].cycles = 4;
    cpu->opcodes[0x1D].bytes = 3;

    cpu->opcodes[0x19].microcode = &ORA;
    cpu->opcodes[0x19].mode = &ABSY;
    cpu->opcodes[0x19].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x19].cycles = 4;
    cpu->opcodes[0x19].bytes = 3;

    //ASL codes

    cpu->opcodes[0x06].microcode = &ASL;
    cpu->opcodes[0x06].mode = &ZPG;
    cpu->opcodes[0x06].name = "Shift Left One Bit";
    cpu->opcodes[0x06].cycles = 5;
    cpu->opcodes[0x06].bytes = 2;

    cpu->opcodes[0x0A].microcode = &ASL;
    cpu->opcodes[0x0A].mode = &ACC;
    cpu->opcodes[0x0A].name = "Shift Left One Bit";
    cpu->opcodes[0x0A].cycles = 2;
    cpu->opcodes[0x0A].bytes = 1;

    cpu->opcodes[0x0E].microcode = &ASL;
    cpu->opcodes[0x0E].mode = &ABS;
    cpu->opcodes[0x0E].name = "Shift Left One Bit";
    cpu->opcodes[0x0E].cycles = 6;
    cpu->opcodes[0x0E].bytes = 3;

    cpu->opcodes[0x16].microcode = &ASL;
    cpu->opcodes[0x16].mode = &ZPGX;
    cpu->opcodes[0x16].name = "Shift Left One Bit zero page + X";
    cpu->opcodes[0x16].cycles = 6;
    cpu->opcodes[0x16].bytes = 2;

    cpu->opcodes[0x1E].microcode = &ASL;
    cpu->opcodes[0x1E].mode = &ABSX;
    cpu->opcodes[0x1E].name = "Shift Left One Bit";
    cpu->opcodes[0x1E].cycles = 7;
    cpu->opcodes[0x1E].bytes = 3;

    //PHP codes

    cpu->opcodes[0x08].microcode = &PHP;
    cpu->opcodes[0x08].mode = &IMP;
    cpu->opcodes[0x08].name = "PHP Push Processor Status to Stack";
    cpu->opcodes[0x08].cycles = 3;
    cpu->opcodes[0x08].bytes = 1;

    //BPL codes

    cpu->opcodes[0x10].microcode = &BPL;
    cpu->opcodes[0x10].mode = &REL;
    cpu->opcodes[0x10].name = "BPL Branch on Negative";
    cpu->opcodes[0x10].cycles = 2;
    cpu->opcodes[0x10].bytes = 2;

    //CLC codes

    cpu->opcodes[0x18].microcode = &CLC;
    cpu->opcodes[0x18].mode = &IMP;
    cpu->opcodes[0x18].name = "Clear Carry Bit";
    cpu->opcodes[0x18].cycles = 2;
    cpu->opcodes[0x18].bytes = 1;

    //JSR codes

    cpu->opcodes[0x20].microcode = &JSR;
    cpu->opcodes[0x20].mode = &ABS;
    cpu->opcodes[0x20].name = "Jump and Save PC register";
    cpu->opcodes[0x20].cycles = 6;
    cpu->opcodes[0x20].bytes = 3;

    //AND codes

    cpu->opcodes[0x21].microcode = &AND;
    cpu->opcodes[0x21].mode = &INDX;
    cpu->opcodes[0x21].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x21].cycles = 6;
    cpu->opcodes[0x21].bytes = 2;

    cpu->opcodes[0x25].microcode = &AND;
    cpu->opcodes[0x25].mode = &ZPG;
    cpu->opcodes[0x25].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x25].cycles = 3;
    cpu->opcodes[0x25].bytes = 2;

    cpu->opcodes[0x29].microcode = &AND;
    cpu->opcodes[0x29].mode = &IMM;
    cpu->opcodes[0x29].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x29].cycles = 2;
    cpu->opcodes[0x29].bytes = 2;

    cpu->opcodes[0x2D].microcode = &AND;
    cpu->opcodes[0x2D].mode = &ABS;
    cpu->opcodes[0x2D].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x2D].cycles = 4;
    cpu->opcodes[0x2D].bytes = 3;

    cpu->opcodes[0x31].microcode = &AND;
    cpu->opcodes[0x31].mode = &INDY;
    cpu->opcodes[0x31].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x31].cycles = 5;
    cpu->opcodes[0x31].bytes = 2;

    cpu->opcodes[0x35].microcode = &AND;
    cpu->opcodes[0x35].mode = &ZPGX;
    cpu->opcodes[0x35].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x35].cycles = 4;
    cpu->opcodes[0x35].bytes = 2;

    cpu->opcodes[0x39].microcode = &AND;
    cpu->opcodes[0x39].mode = &ABSY;
    cpu->opcodes[0x39].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x39].cycles = 4;
    cpu->opcodes[0x39].bytes = 3;

    cpu->opcodes[0x3D].microcode = &AND;
    cpu->opcodes[0x3D].mode = &ABSX;
    cpu->opcodes[0x3D].name = "Perform AND logical operation on M byte over A byte";
    cpu->opcodes[0x3D].cycles = 4;
    cpu->opcodes[0x3D].bytes = 3;

    //BIT codes

    cpu->opcodes[0x24].microcode = &BIT;
    cpu->opcodes[0x24].mode = &ZPG;
    cpu->opcodes[0x24].name = "BIT test bits in accumulator";
    cpu->opcodes[0x24].cycles = 3;
    cpu->opcodes[0x24].bytes = 2;

    cpu->opcodes[0x2C].microcode = &BIT;
    cpu->opcodes[0x2C].mode = &ABS;
    cpu->opcodes[0x2C].name = "BIT test bits in accumulator";
    cpu->opcodes[0x2C].cycles = 4;
    cpu->opcodes[0x2C].bytes = 3;

    //ROL codes

    cpu->opcodes[0x26].microcode = &ROL;
    cpu->opcodes[0x26].mode = &ZPG;
    cpu->opcodes[0x26].name = "Rotate bits left 1 bit zeropage";
    cpu->opcodes[0x26].cycles = 5;
    cpu->opcodes[0x26].bytes = 2;

    cpu->opcodes[0x2A].microcode = &ROL;
    cpu->opcodes[0x2A].mode = &ACC;
    cpu->opcodes[0x2A].name = "Rotate bits left 1 bit zeropage";
    cpu->opcodes[0x2A].cycles = 2;
    cpu->opcodes[0x2A].bytes = 1;

    cpu->opcodes[0x2E].microcode = &ROL;
    cpu->opcodes[0x2E].mode = &ABS;
    cpu->opcodes[0x2E].name = "Rotate bits left 1 bit zeropage";
    cpu->opcodes[0x2E].cycles = 6;
    cpu->opcodes[0x2E].bytes = 3;

    cpu->opcodes[0x36].microcode = &ROL;
    cpu->opcodes[0x36].mode = &ZPGX;
    cpu->opcodes[0x36].name = "Rotate bits left 1 bit zeropage";
    cpu->opcodes[0x36].cycles = 6;
    cpu->opcodes[0x36].bytes = 2;

    cpu->opcodes[0x3E].microcode = &ROL;
    cpu->opcodes[0x3E].mode = &ABSX;
    cpu->opcodes[0x3E].name = "Rotate bits left 1 bit zeropage";
    cpu->opcodes[0x3E].cycles = 6;
    cpu->opcodes[0x3E].bytes = 3;

    //PLP codes

    cpu->opcodes[0x28].microcode = &PLP;
    cpu->opcodes[0x28].mode = &IMP;
    cpu->opcodes[0x28].name = "Pull Processor Status from Stack";
    cpu->opcodes[0x28].cycles = 4;
    cpu->opcodes[0x28].bytes = 1;

    //PLA codes

    cpu->opcodes[0x68].microcode = &PLA;
    cpu->opcodes[0x68].mode = &IMP;
    cpu->opcodes[0x68].name = "Pull Processor Accumulator from Stack";
    cpu->opcodes[0x68].cycles = 4;
    cpu->opcodes[0x68].bytes = 1;

    //BMI codes

    cpu->opcodes[0x30].microcode = &BMI;
    cpu->opcodes[0x30].mode = &REL;
    cpu->opcodes[0x30].name = "Branch on Result Minus";
    cpu->opcodes[0x30].cycles = 2;
    cpu->opcodes[0x30].bytes = 2;

    //ADC functions

    cpu->opcodes[0x61].microcode = &ADC;
    cpu->opcodes[0x61].name = "Add with cary Indirect X";
    cpu->opcodes[0x61].mode = &INDX;
    cpu->opcodes[0x61].bytes = 2;
    cpu->opcodes[0x61].cycles = 6;

    cpu->opcodes[0x65].microcode = &ADC;
    cpu->opcodes[0x65].mode = &ZPG;
    cpu->opcodes[0x65].bytes = 2;
    cpu->opcodes[0x65].cycles = 3;
    cpu->opcodes[0x65].name = "Add with carry zero page";

    cpu->opcodes[0x69].microcode = &ADC;
    cpu->opcodes[0x69].mode = &IMM;
    cpu->opcodes[0x69].name = "Add with carry Immeditae";
    cpu->opcodes[0x69].bytes = 2;
    cpu->opcodes[0x69].cycles = 2;	

    cpu->opcodes[0x6D].microcode = &ADC;
    cpu->opcodes[0x6D].name = "Add with cary Absolute";
    cpu->opcodes[0x6D].mode = &ABS;
    cpu->opcodes[0x6D].bytes = 3;
    cpu->opcodes[0x6D].cycles = 4;

    cpu->opcodes[0x71].microcode = &ADC;
    cpu->opcodes[0x71].name = "Add with cary Indirect Y";
    cpu->opcodes[0x71].mode = &INDY;
    cpu->opcodes[0x71].bytes = 2;
    cpu->opcodes[0x71].cycles = 5;

    cpu->opcodes[0x75].microcode = &ADC;
    cpu->opcodes[0x75].name = "Add with cary Zero Page X";
    cpu->opcodes[0x75].mode = &ZPGX;
    cpu->opcodes[0x75].bytes = 2;
    cpu->opcodes[0x75].cycles = 4;

    cpu->opcodes[0x7D].microcode = &ADC;
    cpu->opcodes[0x7D].name = "Add with cary Absolute X";
    cpu->opcodes[0x7D].mode = &ABSX;
    cpu->opcodes[0x7D].bytes = 3;
    cpu->opcodes[0x7D].cycles = 4;

    cpu->opcodes[0x79].microcode = &ADC;
    cpu->opcodes[0x79].name = "Add with cary Absolute Y";
    cpu->opcodes[0x79].mode = &ABSY;
    cpu->opcodes[0x79].bytes = 3;
    cpu->opcodes[0x79].cycles = 4;

    //BCC codes

    cpu->opcodes[0x90].microcode = &BCC;
    cpu->opcodes[0x90].name = "branch on C = 0";
    cpu->opcodes[0x90].mode = &REL;
    cpu->opcodes[0x90].bytes = 2;
    cpu->opcodes[0x90].cycles = 2;

    //BCS codes

    cpu->opcodes[0xB0].microcode = &BCS;
    cpu->opcodes[0xB0].name = "branch on C = 1";
    cpu->opcodes[0xB0].mode = &REL;
    cpu->opcodes[0xB0].bytes = 2;
    cpu->opcodes[0xB0].cycles = 2;

    //BEQ codes

    cpu->opcodes[0xF0].microcode = &BEQ;
    cpu->opcodes[0xF0].name = "branch on Z = 1";
    cpu->opcodes[0xF0].mode = &REL;
    cpu->opcodes[0xF0].bytes = 2;
    cpu->opcodes[0xF0].cycles = 2;

    //ROR Codes

    cpu->opcodes[0x66].microcode = &ROR;
    cpu->opcodes[0x66].mode = &ZPG;
    cpu->opcodes[0x66].name = "ROR Rotate bits right 1 bit zeropage";
    cpu->opcodes[0x66].cycles = 5;
    cpu->opcodes[0x66].bytes = 2;

    cpu->opcodes[0x6A].microcode = &ROR;
    cpu->opcodes[0x6A].mode = &ACC;
    cpu->opcodes[0x6A].name = "Rotate bits right 1 bit accumulator";
    cpu->opcodes[0x6A].cycles = 2;
    cpu->opcodes[0x6A].bytes = 1;

    cpu->opcodes[0x6E].microcode = &ROR;
    cpu->opcodes[0x6E].mode = &ABS;
    cpu->opcodes[0x6E].name = "Rotate bits right 1 bit absolute";
    cpu->opcodes[0x6E].cycles = 6;
    cpu->opcodes[0x6E].bytes = 3;

    cpu->opcodes[0x76].microcode = &ROR;
    cpu->opcodes[0x76].mode = &ZPGX;
    cpu->opcodes[0x76].name = "Rotate bits right 1 bit zeropage X";
    cpu->opcodes[0x76].cycles = 6;
    cpu->opcodes[0x76].bytes = 2;

    cpu->opcodes[0x7E].microcode = &ROR;
    cpu->opcodes[0x7E].mode = &ABSX;
    cpu->opcodes[0x7E].name = "Rotate bits right 1 bit absolute X";
    cpu->opcodes[0x7E].cycles = 6;
    cpu->opcodes[0x7E].bytes = 3;

    //BNE codes

    cpu->opcodes[0xD0].microcode = &BNE;
    cpu->opcodes[0xD0].name = "branch on Z = 0";
    cpu->opcodes[0xD0].mode = &REL;
    cpu->opcodes[0xD0].bytes = 2;
    cpu->opcodes[0xD0].cycles = 2;

    //CLD

    cpu->opcodes[0xD8].microcode = &CLD;
    cpu->opcodes[0xD8].name = "Clear Decimal";
    cpu->opcodes[0xD8].mode = &IMP;
    cpu->opcodes[0xD8].bytes = 1;
    cpu->opcodes[0xD8].cycles = 2;

    //BPL codes

    cpu->opcodes[0x10].microcode = &BPL;
    cpu->opcodes[0x10].name = "Branch on Result Plus";
    cpu->opcodes[0x10].mode = &REL;
    cpu->opcodes[0x10].bytes = 2;
    cpu->opcodes[0x10].cycles = 2;

    //BVC codes

    cpu->opcodes[0x50].microcode = &BVC;
    cpu->opcodes[0x50].name = "branch on V = 0";
    cpu->opcodes[0x50].mode = &REL;
    cpu->opcodes[0x50].bytes = 2;
    cpu->opcodes[0x50].cycles = 2;

    //EOR codes

    cpu->opcodes[0x41].microcode = &EOR;
    cpu->opcodes[0x41].mode = &INDX;
    cpu->opcodes[0x41].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x41].cycles = 6;
    cpu->opcodes[0x41].bytes = 2;

    cpu->opcodes[0x45].microcode = &EOR;
    cpu->opcodes[0x45].mode = &ZPG;
    cpu->opcodes[0x45].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x45].cycles = 3;
    cpu->opcodes[0x45].bytes = 2;

    cpu->opcodes[0x49].microcode = &EOR;
    cpu->opcodes[0x49].mode = &IMM;
    cpu->opcodes[0x49].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x49].cycles = 2;
    cpu->opcodes[0x49].bytes = 2;

    cpu->opcodes[0x4D].microcode = &EOR;
    cpu->opcodes[0x4D].mode = &ABS;
    cpu->opcodes[0x4D].name = "OR Accumulator with immidate";
    cpu->opcodes[0x4D].cycles = 4;
    cpu->opcodes[0x4D].bytes = 3;

    cpu->opcodes[0x51].microcode = &EOR;
    cpu->opcodes[0x51].mode = &INDY;
    cpu->opcodes[0x51].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x51].cycles = 5;
    cpu->opcodes[0x51].bytes = 2;

    cpu->opcodes[0x55].microcode = &EOR;
    cpu->opcodes[0x55].mode = &ZPGX;
    cpu->opcodes[0x55].name = "OR with zero page memory w/ Accumulator";
    cpu->opcodes[0x55].cycles = 4;
    cpu->opcodes[0x55].bytes = 2;

    cpu->opcodes[0x5D].microcode = &ORA;
    cpu->opcodes[0x5D].mode = &ABSX;
    cpu->opcodes[0x5D].name = "OR Absolute Mem + X w/Accumulator";
    cpu->opcodes[0x5D].cycles = 4;
    cpu->opcodes[0x5D].bytes = 3;

    cpu->opcodes[0x59].microcode = &ORA;
    cpu->opcodes[0x59].mode = &ABSY;
    cpu->opcodes[0x59].name = "OR Mem w/ Accumulator";
    cpu->opcodes[0x59].cycles = 4;
    cpu->opcodes[0x59].bytes = 3;

    //BVS codes

    cpu->opcodes[0x70].microcode = &BVS;
    cpu->opcodes[0x70].name = "branch on V = 1";
    cpu->opcodes[0x70].mode = &REL;
    cpu->opcodes[0x70].bytes = 2;
    cpu->opcodes[0x70].cycles = 2;

    //CLI codes

    cpu->opcodes[0x58].microcode = &CLI;
    cpu->opcodes[0x58].name = "Clear Interrupt Disable Bit";
    cpu->opcodes[0x58].mode = &IMP;
    cpu->opcodes[0x58].bytes = 1;
    cpu->opcodes[0x58].cycles = 2;

    //CLV codes

    cpu->opcodes[0xB8].microcode = &CLV;
    cpu->opcodes[0xB8].name = "Clear Interrupt Disable Bit";
    cpu->opcodes[0xB8].mode = &IMP;
    cpu->opcodes[0xB8].bytes = 1;
    cpu->opcodes[0xB8].cycles = 2;

    //CMP codes

    cpu->opcodes[0xC9].microcode = &CMP;
    cpu->opcodes[0xC9].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xC9].mode = &IMM;
    cpu->opcodes[0xC9].bytes = 2;
    cpu->opcodes[0xC9].cycles = 2;

    cpu->opcodes[0xC5].microcode = &CMP;
    cpu->opcodes[0xC5].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xC5].mode = &ZPG;
    cpu->opcodes[0xC5].bytes = 2;
    cpu->opcodes[0xC5].cycles = 3;

    cpu->opcodes[0xD5].microcode = &CMP;
    cpu->opcodes[0xD5].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xD5].mode = &ZPGX;
    cpu->opcodes[0xD5].bytes = 2;
    cpu->opcodes[0xD5].cycles = 4;

    cpu->opcodes[0xCD].microcode = &CMP;
    cpu->opcodes[0xCD].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xCD].mode = &ABS;
    cpu->opcodes[0xCD].bytes = 3;
    cpu->opcodes[0xCD].cycles = 4;

    cpu->opcodes[0xDD].microcode = &CMP;
    cpu->opcodes[0xDD].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xDD].mode = &ABSX;
    cpu->opcodes[0xDD].bytes = 3;
    cpu->opcodes[0xDD].cycles = 4;

    cpu->opcodes[0xD9].microcode = &CMP;
    cpu->opcodes[0xD9].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xD9].mode = &ABSY;
    cpu->opcodes[0xD9].bytes = 3;
    cpu->opcodes[0xD9].cycles = 4;

    cpu->opcodes[0xC1].microcode = &CMP;
    cpu->opcodes[0xC1].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xC1].mode = &INDX;
    cpu->opcodes[0xC1].bytes = 2;
    cpu->opcodes[0xC1].cycles = 6;

    cpu->opcodes[0xD1].microcode = &CMP;
    cpu->opcodes[0xD1].name = "Compare Memory with Accumulator";
    cpu->opcodes[0xD1].mode = &INDY;
    cpu->opcodes[0xD1].bytes = 2;
    cpu->opcodes[0xD1].cycles = 5;

    //CPX codes

    cpu->opcodes[0xE0].microcode = &CPX;
    cpu->opcodes[0xE0].name = "Compare Memory with X";
    cpu->opcodes[0xE0].mode = &IMM;
    cpu->opcodes[0xE0].bytes = 2;
    cpu->opcodes[0xE0].cycles = 2;

    cpu->opcodes[0xE4].microcode = &CPX;
    cpu->opcodes[0xE4].name = "Compare Memory with X";
    cpu->opcodes[0xE4].mode = &ZPG;
    cpu->opcodes[0xE4].bytes = 2;
    cpu->opcodes[0xE4].cycles = 3;

    cpu->opcodes[0xEC].microcode = &CPX;
    cpu->opcodes[0xEC].name = "Compare Memory with X";
    cpu->opcodes[0xEC].mode = &ABS;
    cpu->opcodes[0xEC].bytes = 3;
    cpu->opcodes[0xEC].cycles = 4;

    //CPY codes

    cpu->opcodes[0xC0].microcode = &CPY;
    cpu->opcodes[0xC0].name = "Compare Memory with Y";
    cpu->opcodes[0xC0].mode = &IMM;
    cpu->opcodes[0xC0].bytes = 2;
    cpu->opcodes[0xC0].cycles = 2;

    cpu->opcodes[0xC4].microcode = &CPY;
    cpu->opcodes[0xC4].name = "Compare Memory with Y";
    cpu->opcodes[0xC4].mode = &ZPG;
    cpu->opcodes[0xC4].bytes = 2;
    cpu->opcodes[0xC4].cycles = 3;

    cpu->opcodes[0xCC].microcode = &CPY;
    cpu->opcodes[0xCC].name = "Compare Memory with Y";
    cpu->opcodes[0xCC].mode = &ABS;
    cpu->opcodes[0xCC].bytes = 3;
    cpu->opcodes[0xCC].cycles = 4;

    //DEC flags

    cpu->opcodes[0xC6].microcode = &DEC;
    cpu->opcodes[0xC6].name = "Decrement Memory by 1";
    cpu->opcodes[0xC6].mode = &ZPG;
    cpu->opcodes[0xC6].bytes = 2;
    cpu->opcodes[0xC6].cycles = 5;

    cpu->opcodes[0xD6].microcode = &DEC;
    cpu->opcodes[0xD6].name = "Decrement Memory by 1";
    cpu->opcodes[0xD6].mode = &ZPGX;
    cpu->opcodes[0xD6].bytes = 2;
    cpu->opcodes[0xD6].cycles = 6;

    cpu->opcodes[0xCE].microcode = &DEC;
    cpu->opcodes[0xCE].name = "Decrement Memory by 1";
    cpu->opcodes[0xCE].mode = &ABS;
    cpu->opcodes[0xCE].bytes = 3;
    cpu->opcodes[0xCE].cycles = 3;

    cpu->opcodes[0xDE].microcode = &DEC;
    cpu->opcodes[0xDE].name = "Decrement Memory by 1";
    cpu->opcodes[0xDE].mode = &ABSX;
    cpu->opcodes[0xDE].bytes = 3;
    cpu->opcodes[0xDE].cycles = 7;

    //DEX codes

    cpu->opcodes[0xCA].microcode = &DEX;
    cpu->opcodes[0xCA].name = "Decrement X by 1";
    cpu->opcodes[0xCA].mode = &IMP;
    cpu->opcodes[0xCA].bytes = 1;
    cpu->opcodes[0xCA].cycles = 2;

    //DEY codes

    cpu->opcodes[0x88].microcode = &DEY;
    cpu->opcodes[0x88].name = "Decrement Y by 1";
    cpu->opcodes[0x88].mode = &IMP;
    cpu->opcodes[0x88].bytes = 1;
    cpu->opcodes[0x88].cycles = 2;

    //EOR 
    cpu->opcodes[0x49].microcode = &EOR;
    cpu->opcodes[0x49].mode = &IMM;
    cpu->opcodes[0x49].bytes = 2;
    cpu->opcodes[0x49].cycles = 2;
    cpu->opcodes[0x49].name = "XOR mem with A"; 

    cpu->opcodes[0x45].microcode = &EOR;
    cpu->opcodes[0x45].mode = &ZPG;
    cpu->opcodes[0x45].bytes = 2;
    cpu->opcodes[0x45].cycles = 3;
    cpu->opcodes[0x45].name = "XOR mem with A";

    cpu->opcodes[0x55].microcode = &EOR;
    cpu->opcodes[0x55].mode = &ZPGX;
    cpu->opcodes[0x55].bytes = 2;
    cpu->opcodes[0x55].cycles = 4; 
    cpu->opcodes[0x55].name = "XOR mem with A";

    cpu->opcodes[0x4D].microcode = &EOR;
    cpu->opcodes[0x4D].mode = &ABS;
    cpu->opcodes[0x4D].bytes = 3;
    cpu->opcodes[0x4D].cycles = 4; 
    cpu->opcodes[0x4D].name = "XOR mem with A";

    cpu->opcodes[0x5D].microcode = &EOR;
    cpu->opcodes[0x5D].mode = &ABSX;
    cpu->opcodes[0x5D].bytes = 3;
    cpu->opcodes[0x5D].cycles = 4; 
    cpu->opcodes[0x5D].name = "XOR mem with A";

    cpu->opcodes[0x59].microcode = &EOR;
    cpu->opcodes[0x59].mode = &ABSY;
    cpu->opcodes[0x59].bytes = 3;
    cpu->opcodes[0x59].cycles = 4; 
    cpu->opcodes[0x59].name = "XOR mem with A";

    cpu->opcodes[0x41].microcode = &EOR;
    cpu->opcodes[0x41].mode = &INDX;
    cpu->opcodes[0x41].bytes = 2;
    cpu->opcodes[0x41].cycles = 6; 
    cpu->opcodes[0x41].name = "XOR mem with A";

    cpu->opcodes[0x51].microcode = &EOR;
    cpu->opcodes[0x51].mode = &INDY;
    cpu->opcodes[0x51].bytes = 2;
    cpu->opcodes[0x51].cycles = 5; 
    cpu->opcodes[0x51].name = "XOR mem with A";

    //INC codes

    cpu->opcodes[0xE6].microcode = &INC;
    cpu->opcodes[0xE6].name = "Increment Memory by 1";
    cpu->opcodes[0xE6].mode = &ZPG;
    cpu->opcodes[0xE6].bytes = 2;
    cpu->opcodes[0xE6].cycles = 5;

    cpu->opcodes[0xF6].microcode = &INC;
    cpu->opcodes[0xF6].name = "Increment Memory by 1";
    cpu->opcodes[0xF6].mode = &ZPGX;
    cpu->opcodes[0xF6].bytes = 2;
    cpu->opcodes[0xF6].cycles = 6;

    cpu->opcodes[0xEE].microcode = &INC;
    cpu->opcodes[0xEE].name = "Increment Memory by 1";
    cpu->opcodes[0xEE].mode = &ABS;
    cpu->opcodes[0xEE].bytes = 3;
    cpu->opcodes[0xEE].cycles = 6;

    cpu->opcodes[0xFE].microcode = &INC;
    cpu->opcodes[0xFE].name = "Increment Memory by 1";
    cpu->opcodes[0xFE].mode = &ABSX;
    cpu->opcodes[0xFE].bytes = 3;
    cpu->opcodes[0xFE].cycles = 7;

    //INX codes

    cpu->opcodes[0xE8].microcode = &INX;
    cpu->opcodes[0xE8].name = "Increment X by 1 imm";
    cpu->opcodes[0xE8].mode = &IMM;
    cpu->opcodes[0xE8].bytes = 1;
    cpu->opcodes[0xE8].cycles = 2;

    //INY codes

    cpu->opcodes[0xC8].microcode = &INY;
    cpu->opcodes[0xC8].name = "Increment Y by 1 imm";
    cpu->opcodes[0xC8].mode = &IMM;
    cpu->opcodes[0xC8].bytes = 1;
    cpu->opcodes[0xC8].cycles = 2;

    //JMP codes

    cpu->opcodes[0x4C].microcode = &JMP;
    cpu->opcodes[0x4C].mode = &ABS;
    cpu->opcodes[0x4C].bytes = 3;
    cpu->opcodes[0x4C].cycles = 3;
    cpu->opcodes[0x4C].name = "JMP to new PC"; 

    cpu->opcodes[0x6C].microcode = &JMP;
    cpu->opcodes[0x6C].mode = &IND;
    cpu->opcodes[0x6C].bytes = 3;
    cpu->opcodes[0x6C].cycles = 5;
    cpu->opcodes[0x6C].name = "JMP to new PC";

    //LDA codes

    cpu->opcodes[0xA9].microcode = &LDA;
    cpu->opcodes[0xA9].mode = &IMM;
    cpu->opcodes[0xA9].bytes = 2;
    cpu->opcodes[0xA9].cycles = 2;
    cpu->opcodes[0xA9].name = "Load value in A"; 

    cpu->opcodes[0xA5].microcode = &LDA;
    cpu->opcodes[0xA5].mode = &ZPG;
    cpu->opcodes[0xA5].bytes = 2;
    cpu->opcodes[0xA5].cycles = 3;
    cpu->opcodes[0xA5].name = "Load value in A";

    cpu->opcodes[0xB5].microcode = &LDA;
    cpu->opcodes[0xB5].mode = &ZPGX;
    cpu->opcodes[0xB5].bytes = 2;
    cpu->opcodes[0xB5].cycles = 4; 
    cpu->opcodes[0xB5].name = "Load value in A";

    cpu->opcodes[0xAD].microcode = &LDA;
    cpu->opcodes[0xAD].mode = &ABS;
    cpu->opcodes[0xAD].bytes = 3;
    cpu->opcodes[0xAD].cycles = 4; 
    cpu->opcodes[0xAD].name = "Load value in A";

    cpu->opcodes[0xBD].microcode = &LDA;
    cpu->opcodes[0xBD].mode = &ABSX;
    cpu->opcodes[0xBD].bytes = 3;
    cpu->opcodes[0xBD].cycles = 4; 
    cpu->opcodes[0xBD].name = "Load value in A";

    cpu->opcodes[0xB9].microcode = &LDA;
    cpu->opcodes[0xB9].mode = &ABSY;
    cpu->opcodes[0xB9].bytes = 3;
    cpu->opcodes[0xB9].cycles = 4; 
    cpu->opcodes[0xB9].name = "Load value in A";

    cpu->opcodes[0xA1].microcode = &LDA;
    cpu->opcodes[0xA1].mode = &INDX;
    cpu->opcodes[0xA1].bytes = 2;
    cpu->opcodes[0xA1].cycles = 6; 
    cpu->opcodes[0xA1].name = "Load value in A";

    cpu->opcodes[0xB1].microcode = &LDA;
    cpu->opcodes[0xB1].mode = &INDY;
    cpu->opcodes[0xB1].bytes = 2;
    cpu->opcodes[0xB1].cycles = 5; 
    cpu->opcodes[0xB1].name = "Load value in A";

    //LDX codes

    cpu->opcodes[0xA2].microcode = &LDX;
    cpu->opcodes[0xA2].mode = &IMM;
    cpu->opcodes[0xA2].bytes = 2;
    cpu->opcodes[0xA2].cycles = 2; 
    cpu->opcodes[0xA2].name = "Load value in X";

    cpu->opcodes[0xA6].microcode = &LDX;
    cpu->opcodes[0xA6].mode = &ZPG;
    cpu->opcodes[0xA6].bytes = 2;
    cpu->opcodes[0xA6].cycles = 3; 
    cpu->opcodes[0xA6].name = "Load value in X";

    cpu->opcodes[0xB6].microcode = &LDX;
    cpu->opcodes[0xB6].mode = &ZPGY;
    cpu->opcodes[0xB6].bytes = 2;
    cpu->opcodes[0xB6].cycles = 4; 
    cpu->opcodes[0xB6].name = "Load value in X";

    cpu->opcodes[0xAE].microcode = &LDX;
    cpu->opcodes[0xAE].mode = &ABS;
    cpu->opcodes[0xAE].bytes = 3;
    cpu->opcodes[0xAE].cycles = 4; 
    cpu->opcodes[0xAE].name = "Load value in X";

    cpu->opcodes[0xBE].microcode = &LDX;
    cpu->opcodes[0xBE].mode = &ABSY;
    cpu->opcodes[0xBE].bytes = 3;
    cpu->opcodes[0xBE].cycles = 4; 
    cpu->opcodes[0xBE].name = "Load value in X";

    //LDY codes

    cpu->opcodes[0xA0].microcode = &LDY;
    cpu->opcodes[0xA0].mode = &IMM;
    cpu->opcodes[0xA0].bytes = 2;
    cpu->opcodes[0xA0].cycles = 2; 
    cpu->opcodes[0xA0].name = "Load value in Y";

    cpu->opcodes[0xA4].microcode = &LDY;
    cpu->opcodes[0xA4].mode = &ZPG;
    cpu->opcodes[0xA4].bytes = 2;
    cpu->opcodes[0xA4].cycles = 3; 
    cpu->opcodes[0xA4].name = "Load value in Y";

    cpu->opcodes[0xB4].microcode = &LDY;
    cpu->opcodes[0xB4].mode = &ZPGX;
    cpu->opcodes[0xB4].bytes = 2;
    cpu->opcodes[0xB4].cycles = 4; 
    cpu->opcodes[0xB4].name = "Load value in Y";

    cpu->opcodes[0xAC].microcode = &LDY;
    cpu->opcodes[0xAC].mode = &ABS;
    cpu->opcodes[0xAC].bytes = 3;
    cpu->opcodes[0xAC].cycles = 4; 
    cpu->opcodes[0xAC].name = "Load value in Y";

    cpu->opcodes[0xBC].microcode = &LDY;
    cpu->opcodes[0xBC].mode = &ABSX;
    cpu->opcodes[0xBC].bytes = 3;
    cpu->opcodes[0xBC].cycles = 4; 
    cpu->opcodes[0xBC].name = "Load value in Y";

    //LSR Codes

    cpu->opcodes[0x4A].microcode = &LSR;
    cpu->opcodes[0x4A].mode = &ACC;
    cpu->opcodes[0x4A].bytes = 1;
    cpu->opcodes[0x4A].cycles = 2; 
    cpu->opcodes[0x4A].name = "LSR shift right";

    cpu->opcodes[0x46].microcode = &LSR;
    cpu->opcodes[0x46].mode = &ZPG;
    cpu->opcodes[0x46].bytes = 2;
    cpu->opcodes[0x46].cycles = 3; 
    cpu->opcodes[0x46].name = "LSR shift right";

    cpu->opcodes[0x56].microcode = &LSR;
    cpu->opcodes[0x56].mode = &ZPGX;
    cpu->opcodes[0x56].bytes = 2;
    cpu->opcodes[0x56].cycles = 4; 
    cpu->opcodes[0x56].name = "LSR shift right";

    cpu->opcodes[0x4E].microcode = &LSR;
    cpu->opcodes[0x4E].mode = &ABS;
    cpu->opcodes[0x4E].bytes = 3;
    cpu->opcodes[0x4E].cycles = 4; 
    cpu->opcodes[0x4E].name = "LSR shift right";

    cpu->opcodes[0x5E].microcode = &LSR;
    cpu->opcodes[0x5E].mode = &ABSX;
    cpu->opcodes[0x5E].bytes = 3;
    cpu->opcodes[0x5E].cycles = 4; 
    cpu->opcodes[0x5E].name = "Load value in Y";

    //NOP
    cpu->opcodes[0xEA].microcode = &NOP;
    cpu->opcodes[0xEA].mode = &IMP;
    cpu->opcodes[0xEA].bytes = 1;
    cpu->opcodes[0xEA].cycles = 2;
    cpu->opcodes[0xEA].name = "NOP do nothing";

    //PHA codes

    cpu->opcodes[0x48].microcode = &PHA;
    cpu->opcodes[0x48].mode = &IMP;
    cpu->opcodes[0x48].bytes = 1;
    cpu->opcodes[0x48].cycles = 3; 
    cpu->opcodes[0x48].name = "Push Acc to Stack";

    //RTI codes

    cpu->opcodes[0x40].microcode = &RTI;
    cpu->opcodes[0x40].mode = &IMP;
    cpu->opcodes[0x40].bytes = 1;
    cpu->opcodes[0x40].cycles = 6; 
    cpu->opcodes[0x40].name = "Pull SR and PC from stack";

    //SBC codes

    cpu->opcodes[0xE9].microcode = &SBC;
    cpu->opcodes[0xE9].bytes = 2;
    cpu->opcodes[0xE9].cycles = 2;
    cpu->opcodes[0xE9].mode = &IMM;
    cpu->opcodes[0xE9].name = "Subtract Memory from Accumulator with Borrow";

    cpu->opcodes[0xE5].microcode = &SBC;
    cpu->opcodes[0xE5].bytes = 2;
    cpu->opcodes[0xE5].cycles = 3;
    cpu->opcodes[0xE5].mode = &ZPG;
    cpu->opcodes[0xE5].name = "Subtract Memory from Accumulator with Borrow";

    cpu->opcodes[0xF5].microcode = &SBC;
    cpu->opcodes[0xF5].bytes = 2;
    cpu->opcodes[0xF5].cycles = 4;
    cpu->opcodes[0xF5].mode = &ZPGX;
    cpu->opcodes[0xF5].name = "Subtract Memory from Accumulator with Borrow";

    cpu->opcodes[0xED].microcode = &SBC;
    cpu->opcodes[0xED].bytes = 3;
    cpu->opcodes[0xED].cycles = 4;
    cpu->opcodes[0xED].mode = &ABS;
    cpu->opcodes[0xED].name = "Subtract Memory from Accumulator with Borrow";

    cpu->opcodes[0xFD].microcode = &SBC;
    cpu->opcodes[0xFD].bytes = 3;
    cpu->opcodes[0xFD].cycles = 4;
    cpu->opcodes[0xFD].mode = &ABSX;
    cpu->opcodes[0xFD].name = "Subtract Memory from Accumulator with Borrow";

    cpu->opcodes[0xF9].microcode = &SBC;
    cpu->opcodes[0xF9].bytes = 3;
    cpu->opcodes[0xF9].cycles = 4;
    cpu->opcodes[0xF9].mode = &ABSY;
    cpu->opcodes[0xF9].name = "Subtract Memory from Accumulator with Borrow";

    cpu->opcodes[0xE1].microcode = &SBC;
    cpu->opcodes[0xE1].bytes = 2;
    cpu->opcodes[0xE1].cycles = 6;
    cpu->opcodes[0xE1].mode = &INDX;
    cpu->opcodes[0xE1].name = "Subtract Memory from Accumulator with Borrow";

    cpu->opcodes[0xF1].microcode = &SBC;
    cpu->opcodes[0xF1].bytes = 2;
    cpu->opcodes[0xF1].cycles = 5;
    cpu->opcodes[0xF1].mode = &INDY;
    cpu->opcodes[0xF1].name = "Subtract Memory from Accumulator with Borrow";

    //SEC codes

    cpu->opcodes[0x38].microcode = &SEC;
    cpu->opcodes[0x38].name = "Set Carry";
    cpu->opcodes[0x38].bytes = 1;
    cpu->opcodes[0x38].cycles = 2;
    cpu->opcodes[0x38].mode = &IMP;

    //SED codes

    cpu->opcodes[0xF8].microcode = &SED;
    cpu->opcodes[0xF8].name = "Set Decimal";
    cpu->opcodes[0xF8].bytes = 1;
    cpu->opcodes[0xF8].cycles = 2;
    cpu->opcodes[0xF8].mode = &IMP;

    //SEI codes

    cpu->opcodes[0x78].microcode = &SEI;
    cpu->opcodes[0x78].name = "Set Interrupt";
    cpu->opcodes[0x78].bytes = 1;
    cpu->opcodes[0x78].cycles = 2;
    cpu->opcodes[0x78].mode = &IMP;

    //STA codes

    cpu->opcodes[0x85].microcode = &STA;
    cpu->opcodes[0x85].name = "Store Acc in Mem";
    cpu->opcodes[0x85].mode = &ZPG;
    cpu->opcodes[0x85].bytes = 2;
    cpu->opcodes[0x85].cycles = 3;

    cpu->opcodes[0x95].microcode = &STA;
    cpu->opcodes[0x95].name = "Store Acc in Mem";
    cpu->opcodes[0x95].mode = &ZPGX;
    cpu->opcodes[0x95].bytes = 2;
    cpu->opcodes[0x95].cycles = 4;

    cpu->opcodes[0x8D].microcode = &STA;
    cpu->opcodes[0x8D].name = "Store Acc in Mem";
    cpu->opcodes[0x8D].mode = &ABS;
    cpu->opcodes[0x8D].bytes = 3;
    cpu->opcodes[0x8D].cycles = 4;

    cpu->opcodes[0x9D].microcode = &STA;
    cpu->opcodes[0x9D].name = "Store Acc in Mem";
    cpu->opcodes[0x9D].mode = &ABSX;
    cpu->opcodes[0x9D].bytes = 3;
    cpu->opcodes[0x9D].cycles = 5;

    cpu->opcodes[0x99].microcode = &STA;
    cpu->opcodes[0x99].name = "Store Acc in Mem";
    cpu->opcodes[0x99].mode = &ABSY;
    cpu->opcodes[0x99].bytes = 3;
    cpu->opcodes[0x99].cycles = 5;

    cpu->opcodes[0x81].microcode = &STA;
    cpu->opcodes[0x81].name = "Store Acc in Mem";
    cpu->opcodes[0x81].mode = &INDX;
    cpu->opcodes[0x81].bytes = 2;
    cpu->opcodes[0x81].cycles = 6;

    cpu->opcodes[0x91].microcode = &STA;
    cpu->opcodes[0x91].name = "Store Acc in Mem";
    cpu->opcodes[0x91].mode = &INDY;
    cpu->opcodes[0x91].bytes = 2;
    cpu->opcodes[0x91].cycles = 6;

    //STX codes

    cpu->opcodes[0x86].microcode = &STX;
    cpu->opcodes[0x86].name = "Write X reg to bus";
    cpu->opcodes[0x86].mode = &ZPG;
    cpu->opcodes[0x86].bytes = 2;
    cpu->opcodes[0x86].cycles = 3;

    cpu->opcodes[0x96].microcode = &STX;
    cpu->opcodes[0x96].name = "Write X reg to bus1";
    cpu->opcodes[0x96].mode = &ZPGY;
    cpu->opcodes[0x96].bytes = 2;
    cpu->opcodes[0x96].cycles = 4;

    cpu->opcodes[0x8E].microcode = &STX;
    cpu->opcodes[0x8E].name = "Write X reg to bus";
    cpu->opcodes[0x8E].mode = &ABS;
    cpu->opcodes[0x8E].bytes = 3;
    cpu->opcodes[0x8E].cycles = 4;

    //STY codes

    cpu->opcodes[0x84].microcode = &STY;
    cpu->opcodes[0x84].name = "Write Y reg to bus";
    cpu->opcodes[0x84].mode = &ZPG;
    cpu->opcodes[0x84].bytes = 2;
    cpu->opcodes[0x84].cycles = 3;

    cpu->opcodes[0x94].microcode = &STY;
    cpu->opcodes[0x94].name = "Write Y reg to bus1";
    cpu->opcodes[0x94].mode = &ZPGX;
    cpu->opcodes[0x94].bytes = 2;
    cpu->opcodes[0x94].cycles = 4;

    cpu->opcodes[0x8C].microcode = &STY;
    cpu->opcodes[0x8C].name = "Write Y reg to bus";
    cpu->opcodes[0x8C].mode = &ABS;
    cpu->opcodes[0x8C].bytes = 3;
    cpu->opcodes[0x8C].cycles = 4;

    //TAX codes (lmao)

    cpu->opcodes[0xAA].microcode = &TAX;
    cpu->opcodes[0xAA].mode = &IMP;
    cpu->opcodes[0xAA].name = "Transfer Accumulator to X";
    cpu->opcodes[0xAA].cycles = 2;
    cpu->opcodes[0xAA].bytes = 1;

    //TAY codes

    cpu->opcodes[0xA8].microcode = &TAY;
    cpu->opcodes[0xA8].mode = &IMP;
    cpu->opcodes[0xA8].name = "Transfer Accumulator to Y";
    cpu->opcodes[0xA8].cycles = 2;
    cpu->opcodes[0xA8].bytes = 1;

    //TSX codes

    cpu->opcodes[0xBA].microcode = &TSX;
    cpu->opcodes[0xBA].mode = &IMP;
    cpu->opcodes[0xBA].name = "Transfer SP to X";
    cpu->opcodes[0xBA].cycles = 2;
    cpu->opcodes[0xBA].bytes = 1;

    //TXA codes

    cpu->opcodes[0x8A].microcode = &TXA;
    cpu->opcodes[0x8A].mode = &IMP;
    cpu->opcodes[0x8A].name = "Transfer X to Acc";
    cpu->opcodes[0x8A].cycles = 2;
    cpu->opcodes[0x8A].bytes = 1;

    //TXS codes

    cpu->opcodes[0x9A].microcode = &TXS;
    cpu->opcodes[0x9A].mode = &IMP;
    cpu->opcodes[0x9A].name = "Transfer X to Stack Register";
    cpu->opcodes[0x9A].cycles = 2;
    cpu->opcodes[0x9A].bytes = 1;

    //TYA codes

    cpu->opcodes[0x98].microcode = &TYA;
    cpu->opcodes[0x98].mode = &IMP;
    cpu->opcodes[0x98].name = "Transfer Y to Acc";
    cpu->opcodes[0x98].cycles = 2;
    cpu->opcodes[0x98].bytes = 1;

    //RTS codes

    cpu->opcodes[0x60].microcode = &RTS;
    cpu->opcodes[0x60].mode = &IMP;
    cpu->opcodes[0x60].name = "Return from subroutine";
    cpu->opcodes[0x60].cycles = 6;
    cpu->opcodes[0x60].bytes = 1;
}

void printRegisters(CPU * __restrict__ cpu){
  printf("N: %i\n", cpu->SR.flags.Negative);
  printf("V: %i\n", cpu->SR.flags.Overflow);
  printf("B: %i\n", cpu->SR.flags.Break);
  printf("D: %i\n", cpu->SR.flags.Decimal);
  printf("I: %i\n", cpu->SR.flags.Interrupt);
  printf("Z: %i\n", cpu->SR.flags.Zero);
  printf("C: %i\n", cpu->SR.flags.Carry);
 
}

void printCpu(CPU * __restrict__ cpu){
    #define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
    #define BYTE_TO_BINARY(byte_)  \
  (byte_ & 0x80 ? '1' : '0'), \
  (byte_ & 0x40 ? '1' : '0'), \
  (byte_ & 0x20 ? '1' : '0'), \
  (byte_ & 0x10 ? '1' : '0'), \
  (byte_ & 0x08 ? '1' : '0'), \
  (byte_ & 0x04 ? '1' : '0'), \
  (byte_ & 0x02 ? '1' : '0'), \
  (byte_ & 0x01 ? '1' : '0') 
    
    
    printf("PC: %#08x\n", cpu->PC);
    printf("SP: %i hex %02X, (%i)\n", cpu->SP, cpu->SP, 0xFFFF - cpu->SP);
    printf("A: %i hex %02X\n", cpu->A, cpu->A);
    printf("X: %i\n", cpu->X);
    printf("Y: %i\n", cpu->Y);

    printf("SR: "BYTE_TO_BINARY_PATTERN "\n", BYTE_TO_BINARY(cpu->SR.data));
    printf("SR.data: 0x%02X\n", cpu->SR.data);
    #undef BYTE_TO_BINARY_PATTERN
    #undef BYTE_TO_BINARY
}

enum ERRORS{
    NORMAL,
    OPCODES_OB,
    BUS_OB,
    UNALLOC_MICRO,
    UNALLOC_MODE,
    UNALLOC_NAME,
    UNALLOC_OP,
    UNALLOC_INST,
    UNALLOC_CPU,
    UNKNOWN_MICRO,
    UNKNOWN_MODE,
    UNKNOWN_BYTES,
    UNKNOWN_CYCLES
};

void raiseError(unsigned int err, CPU * __restrict__ cpu){
    const char * ERROR_STR[13]= {
        "Unknown Error",
        "Tried to access a opcode that doesn't exist",
        "Tried to access bus data that is out of range",
        "Micro code was not allocated / set",
        "Mode code was not allocated",
        "Name was not allocated",
        "Opcode was not allocated",
        "Instruction was not allocated",
        "CPU was not allocated",
        "Microcode is in valid range but doesn't point to a valid microcode",
        "Mode is in valid range but doesn't point to a vaild mode",
        "Bytes was allocated but not set",
        "Cycles was allocated but not set",
    };

    fprintf(stderr, "%s\n", ERROR_STR[err]);
        fprintf(stderr, "CRASH!!!!\n\n");
        fprintf(stderr, "\tLOCATION: %#06x\n", cpu->PC);
        fprintf(stderr, "\tOPCODE: %#06x\n", busRead8(cpu->PC));
        if(err != UNALLOC_NAME && err != UNALLOC_MICRO)
            fprintf(stderr, "\tNAME: \"%s\"\n", cpu->opcodes[busRead8(cpu->PC)].name);
        else
            fprintf(stderr, "\tNAME: \"(NULL)\"\n");
        fprintf(stderr, "\tMICRO: %p\n", cpu->opcodes[busRead8(cpu->PC)].microcode);
        fprintf(stderr, "\tMODE: %p\n", cpu->opcodes[busRead8(cpu->PC)].mode);
    exit(err);
}

void print_stack(CPU * __restrict__ cpu){
    char x[2] = {'.', '.'};
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    for(word i = STACK_RAM_OFFSET; i < STACK_RAM_OFFSET + 0xFF; i++){
        if(i == cpu->SP + STACK_RAM_OFFSET){
            x[0] = '[';
            x[1] = ']';
        }
        printf(" %c%02X%c ", x[0], busRead8(i), x[1]);
        x[0] = '.';
        x[1] = '.';
    }
    printf("\n");
}

void handleErrors(CPU * __restrict__ cpu){
    if(cpu == NULL){
       raiseError(UNALLOC_CPU, cpu);
    }
    if(cpu->PC > BUS_SIZE){
        raiseError(BUS_OB, cpu);
    }
    if(cpu->opcodes == NULL){
        raiseError(UNALLOC_OP, cpu);
    }

    struct instruction cur_inst = cpu->opcodes[busRead8(cpu->PC)];

    if(cur_inst.microcode == NULL){
        raiseError(UNALLOC_MICRO, cpu);
    }
    if(cur_inst.mode == NULL){
        raiseError(UNALLOC_MODE, cpu);
    }
    if(cur_inst.name == NULL){
        raiseError(UNALLOC_NAME, cpu);
    }
    if(cur_inst.cycles == 0){
        raiseError(UNKNOWN_CYCLES, cpu);
    }
    if(cur_inst.bytes == 0){
        raiseError(UNKNOWN_BYTES, cpu);
    }
    /*
    if(cur_inst.microcode < ORA || cur_inst.microcode > TYA){
        raiseError(UNKNOWN_MICRO, cpu);
    }
    */

}

/*void printErrorCodes(CPU * cpu){
    printf("0x02 = %02X, 0x03 = %03X\n", )
}*/

void initCpu(CPU * __restrict__ cpu){
    cpu->PC = 0;
    cpu->A = 0;
    cpu->X = 0;
    cpu->Y = 0;
    cpu->SR.data = 0x6C;
    cpu->SP = 0xFD;
    
    initOpcodeReg(cpu);//import the stuff about each microcode, stuff like bytes per instruction, cycles, adressing mode, and operation in the array, where the value in the array is the byte that triggers that action for the CPU
}

void cpuNmi(CPU * cpu){

    //push PC to stack
    busWrite8(cpu->SP + STACK_RAM_OFFSET, cpu->PC >> 8);
    cpu->SP--;

    busWrite8(cpu->SP + STACK_RAM_OFFSET, cpu->PC & 0x00FF);
    cpu->SP--;

    cpu->SR.flags.Break = 0;
    cpu->SR.flags.Interrupt = 1; //set flags
    cpu->SR.flags.ignored = 1;

    busWrite8(cpu->SP + STACK_RAM_OFFSET, cpu->SR.data); //push status register to stack
    cpu->SP--;

    //Read new address from NMI vector
    cpu->PC = decodeRomPCVector(ROM_VECTOR_NMI);
}

void cpuClock(CPU * cpu){


    handleErrors(cpu);

    cpu->pcNeedsInc = true;
    word args = 0;
    if(cpu->opcodes[busRead8(cpu->PC)].bytes > 2){ //if args is 16bit shifts the first byte to the hi byte (they get reversed basically)
        args = busRead8(cpu->PC + 2) << 8;
    }
    args |= busRead8(cpu->PC + 1); //add first arg byte

    byte sizeOfInstruction = cpu->opcodes[busRead8(cpu->PC)].bytes;

    //
    //
    //EXECUTION HERE
    //
    //

    byte execOPdata = busRead8(cpu->PC); //get OPCODE byte (ex 0x4C)
    struct instruction execOP = cpu->opcodes[execOPdata]; //fetch OPCODE data, addressing mode and microcode (ex 0x4C -> JMP)

    execOP.microcode(cpu, args, execOP.mode); //EXECUTE OPCODE

    //
    //
    //EXECUTION DONE
    //
    //

    if(cpu->pcNeedsInc){
        cpu->PC += sizeOfInstruction;
    } //increase program counter to move on to the next instruction
}
