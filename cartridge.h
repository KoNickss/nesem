#include "stdlib.h"
#include "common.h"

//extern? void (*mapper_read)(word);
//void (*mapper_write)(bool);

extern word romStartAddress;

#define K_SIZE 1024

#define PRG_BANK_SIZE (16 * K_SIZE)
#define CHR_BANK_SIZE (8 * K_SIZE)

#define DEFAULT_PRG_SIZE (PRG_BANK_SIZE * 1)
#define DEFAULT_CHR_SIZE (CHR_BANK_SIZE * 1)

#define GET_PRG_BANK_SIZE(banks) (PRG_BANK_SIZE * banks)
#define GET_CHR_BANK_SIZE(banks) (CHR_BANK_SIZE * banks)

enum FLAG6_MIRRORING_MODE{
    VERTICAL,
    HORIZONTAL
};

enum FLAG9_TV_MODE{
    NTSC,
    PAL
};

typedef union {
    struct{
        struct {
            byte mirroringMode : 1;
            byte battery : 1;
            byte trainer : 1;
            byte disableMirroring : 1;

            byte lowerMapperNumber : 4;
        }flag6;

        struct {
            byte unisystem : 1;
            byte playChoice10 : 1;
            byte INes_version : 2;
            byte disableMirroring : 1;

            byte upperMapperNumber : 4;
        }flag7;

        struct {
            byte PRG_RAM_SIZE;
        }flag8;

        struct {
            byte tvMode : 1;
            byte padding : 7;
        }flag9;
    };

    byte array[5];
}Flags;


typedef struct{
    byte PRG_BANKS;
    byte CHR_BANKS;

    Flags flags;

    byte padding[5];
}HEADER;


void initBanks(char * fn);

word mapper000_Read(word address, bool ppu);
bool mapper000_Write(word address, byte data, bool ppu);

