#include "bus.h"

//Credits to wiki.nesdev.com for these register graphs
//https://wiki.nesdev.com/w/index.php?title=PPU_registers

typedef struct{
    union{
        struct __attribute__((packed, aligned(1))){
            byte coarseX : 5;
            byte coarseY : 5;
            byte nameTableID : 2;
            byte fineY : 3;
            byte unused: 1;

        }field;

        struct{
            byte lsb;
            byte msb;

        }bytes;

        word data;
    };
}locationRegister;



//Start of PPU struct
typedef struct{
    /*
        7  bit  0
        ---- ----
        VPHB SINN
        |||| ||||
        |||| ||++- Base nametable address
        |||| ||    (0 = $2000; 1 = $2400; 2 = $2800; 3 = $2C00)
        |||| |+--- VRAM address increment per CPU read/write of PPUDATA
        |||| |     (0: add 1, going across; 1: add 32, going down)
        |||| +---- Sprite pattern table address for 8x8 sprites
        ||||       (0: $0000; 1: $1000; ignored in 8x16 mode)
        |||+------ Background pattern table address (0: $0000; 1: $1000)
        ||+------- Sprite size (0: 8x8 pixels; 1: 8x16 pixels)
        |+-------- PPU master/slave select
        |          (0: read backdrop from EXT pins; 1: output color on EXT pins)
        +--------- Generate an NMI at the start of the
                vertical blanking interval (0: off; 1: on)
    */
    union {
        struct {
            byte nameTableID : 2;
            byte vramIncrement : 1;
            byte spritePatternTable : 1;
            byte backgroundPatternTable : 1;
            byte spriteSize: 1;
            byte nmiVerticalBlank : 1;
        };
        byte full;
    }control;

    /*
    7  bit  0
    ---- ----
    VSO. ....
    |||| ||||
    |||+-++++- Least significant bits previously written into a PPU register
    |||        (due to register not being updated for this address)
    ||+------- Sprite overflow. The intent was for this flag to be set
    ||         whenever more than eight sprites appear on a scanline, but a
    ||         hardware bug causes the actual behavior to be more complicated
    ||         and generate false positives as well as false negatives; see
    ||         PPU sprite evaluation. This flag is set during sprite
    ||         evaluation and cleared at dot 1 (the second dot) of the
    ||         pre-render line.
    |+-------- Sprite 0 Hit.  Set when a nonzero pixel of sprite 0 overlaps
    |          a nonzero background pixel; cleared at dot 1 of the pre-render
    |          line.  Used for raster timing.
    +--------- Vertical blank has started (0: not in vblank; 1: in vblank).
            Set at dot 1 of line 241 (the line *after* the post-render
            line); cleared after reading $2002 and at dot 1 of the
            pre-render line.
    */
    union{
        struct {
            byte unused : 5;
            byte spriteOverflow : 1;
            byte sprite0Hit : 1;
            byte vblank : 1;
        };
        byte full;
    }status;

    union{
        struct{
            byte greyscale : 1;
            byte showBackdropDebug : 1;
            byte showSpritesDebug : 1;
            byte unused : 5;
        };
        byte full;
    }mask;


    byte dataByteBuffer;

    locationRegister vReg; //Search for: "IMPORTANT V SYNC" in ppu.c to see moments where the 2 sync

    locationRegister tReg; //Search for: "IMPORTANT V SYNC" in ppu.c to see moments where the 2 sync

    byte xReg;

    bool expectingLsb;
}PPU;

void initPpu();
void dumpPpuBus();

void ppuRegWrite(word address, byte data);
byte ppuRegRead(word address);