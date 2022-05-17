#include "stdlib.h"
#include "common.h"

//extern? void (*mapper_read)(word);
//void (*mapper_write)(bool);


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
            byte mirroring_mode : 1;
            byte battery : 1;
            byte trainer : 1;
            byte disable_mirroring : 1;

            byte lower_mapper_number : 4;
        }flag6;

        struct {
            byte unisystem : 1;
            byte play_choice_10 : 1;
            byte INes_version : 2;
            byte disable_mirroring : 1;

            byte upper_mapper_number : 4;
        }flag7;

        struct {
            byte PRG_RAM_SIZE;
        }flag8;

        struct {
            byte tv_mode : 1;
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


void init_banks(char * fn);

word mapper000_read(word address, bool ppu);
bool mapper000_write(word address, byte data, bool ppu);

