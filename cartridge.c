#include "cartridge.h"
#include "bus.h"

HEADER Header;
byte PRGROM[0xFFFF];
byte CHRROM[0xFFFF];

word not_handling_this = 0x100; //0xFF + 1

word rom_start_address = 0x0;

void loadRomfileHeader(FILE * romfile){
    byte verificationToken[3] = "NES";
    for(byte i = 0; i < 3; i++)
        if(verificationToken[i] != getc(romfile)){
            fprintf(stderr, "ERR: This is not a NES Rom!!!\n");
            exit(EXIT_FAILURE);
            return;
        }
    getc(romfile); //get over the DOS EOF byte
    Header.PRG_BANKS = getc(romfile);
    Header.CHR_BANKS = getc(romfile);

    for(byte i = 0; i < 5; i++){
        Header.flags.array[i] = getc(romfile);
    }
    for(byte i = 0; i < 5; i++){ //Remove padding
        getc(romfile);
    }
}

void initBanks(char name[]){
    FILE * romfile;
    romfile = fopen(name, "rb");

    #ifdef DEBUG
        if(romfile == NULL){
            fprintf(stderr, "ERR: file \"%s\" could not be opened for reading!\n", name);
            exit(EXIT_FAILURE);
        }
    #endif
    
    loadRomfileHeader(romfile);
    
    if(Header.flags.flag6.trainer) //if trainer data
        fseek(romfile, 512, SEEK_CUR);
    

    for(word i = 0; i < GET_PRG_BANK_SIZE(Header.PRG_BANKS); i++){
        PRGROM[i] = getc(romfile);
    }

    for(word i = 0; i < GET_CHR_BANK_SIZE(Header.CHR_BANKS); i++){
        CHRROM[i] = getc(romfile);
    }

    fclose(romfile);

    //Read rom start Address
    #define ROM_START_VECTOR_ADDR (0xFFFC)
    rom_start_address = busRead16(ROM_START_VECTOR_ADDR);
}

word mapper000_Read(word address, bool ppu){
    if(!ppu){ //if not ppu talking
        if(!((address <= 0xFFFF) && (address >= 0x8000))){ //if not in the address range the mapper functions in
            return not_handling_this; //not_handling_this is a 16 bit data integer, no byte will return this ever so this is our "not handling this" return code
        }else{
            if(Header.PRG_BANKS > 1){ //32K model
                return PRGROM[address - 0x8000 + 0x4];
            }else{ //16K model
                return PRGROM[(address - 0x8000 + 0x4) % 0x3FFF];
            }
        }
    }else{ //if PPU
        if(Header.CHR_BANKS == 0) return not_handling_this; //if no CHR banks, nothing to mirror
        else if (address <= 0x2000) return CHRROM[address - 0x2000];
		else return not_handling_this;
    }
}

bool mapper000_Write(word address, byte data, bool ppu){
    return false; //no PRGRAM configured in this cartridge, so all writes get denied
};
