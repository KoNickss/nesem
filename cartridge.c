#include "cartridge.h"
#include "bus.h"

static HEADER Header;
static byte PRGROM[K_SIZE * 16 * 16];
static byte CHRROM[K_SIZE * 8 * 16];

word not_handling_this = 0x100; //0xFF + 1

word romStartAddress = 0x0;

bool _verticalMirroring;

bool isVerticalMirroring(){
    return _verticalMirroring;
}

void loadRomfileHeader(FILE * romfile){
    byte verificationToken[3] = "NES";
    for(byte i = 0; i < 3; i++)
        if(verificationToken[i] != getc(romfile)){
            PRINT_ERROR("rom", "This is not a NES Rom!!!");
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

    if(romfile == NULL){
        PRINT_ERROR("rom", "File \"%s\" could not be opened for reading!", name);
        exit(EXIT_FAILURE);
    }

    loadRomfileHeader(romfile);

    _verticalMirroring = Header.flags.flag6.mirroringMode;

    if(Header.flags.flag6.trainer){ //if trainer data
        int result = fseek(romfile, 512, SEEK_CUR);
        SMART_ASSERT(result >= 0, "Rom says it has trainer data but ROM is not big enough or other IO error!");
    }


    SMART_ASSERT(GET_PRG_BANK_SIZE(Header.PRG_BANKS) <= sizeof(PRGROM), "You attempted to load a NES ROM with too large of a PRG bank! PRG bank count = %u", Header.PRG_BANKS);
    SMART_ASSERT(GET_CHR_BANK_SIZE(Header.CHR_BANKS) <= sizeof(CHRROM), "You attempted to load a NES ROM with too large of a CHR bank! CHR bank count = %u", Header.PRG_BANKS);

    int read_result = 0;
    read_result=fread(PRGROM, 1, GET_PRG_BANK_SIZE(Header.PRG_BANKS), romfile);
    SMART_ASSERT(read_result > 0, "Could not read PRGROM");
    read_result=fread(CHRROM, 1, GET_CHR_BANK_SIZE(Header.CHR_BANKS), romfile);
    SMART_ASSERT(read_result > 0, "Could not read CHRROM");

    unsigned long read_size = ftell(romfile);
    fseek(romfile, 0, SEEK_END);
    unsigned long file_size = ftell(romfile);
    SMART_WARN(read_size == file_size, "Bytes read does not match the file size! Nesem Reported = 0x%lX, File Size = 0x%lX!\n", read_size, file_size);

    fclose(romfile);

    //Read rom start Address
    #define ROM_START_VECTOR_ADDR (0xFFFC)
    romStartAddress = busRead16(ROM_START_VECTOR_ADDR);
}

word mapper000_Read(word address, bool ppu){
    if(!ppu){ //if not ppu talking
        if(!((address <= 0xFFFF) && (address >= 0x8000))){ //if not in the address range the mapper functions in
            return not_handling_this; //not_handling_this is a 16 bit data integer, no byte will return this ever so this is our "not handling this" return code
        }else{
            if(Header.PRG_BANKS > 1){ //32K model
                return PRGROM[address - 0x8000];
            }else{ //16K model
                return PRGROM[(address - 0x8000) % 0x4000];
            }
        }
    }else{ //if PPU
        if(Header.CHR_BANKS == 0){
        	return not_handling_this; //if no CHR banks, nothing to mirror
        }else if (address <= 0x2000){
        	return CHRROM[address];
		}else{
			return not_handling_this;
		}
    }
}

bool mapper000_Write(word address, byte data, bool ppu){
    return false; //no PRGRAM configured in this cartridge, so all writes get denied
};
