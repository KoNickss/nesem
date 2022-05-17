#include "ppu.h"




void ppuWrite(PPU * Ppu, word address, byte data){
    address -= 0x2000;
    switch(address){
        case 0: //ppuctrl

            Ppu->control.full = data;

        break;

        case 1: //ppumask

            Ppu->mask.full = data;

        break;

        case 2: //ppustatus

            Ppu->status.full = data;

        break;

        case 3: //oamAddress

        break;

        case 4: //oamData

        break;

        case 5: //ppuScroll

            if(Ppu->scroll.expectingY){
                Ppu->scroll.y = data;
            }else{
                Ppu->scroll.x = data;
            }

            Ppu->scroll.expectingY = !Ppu->scroll.expectingY;

        break;

        case 6: //ppuAddr

            if(Ppu->address.expectLsb){
                Ppu->address.lsb = data;
            }else{
                Ppu->address.msb = data;
            }

            Ppu->address.expectLsb = !Ppu->address.expectLsb;

        break;

        case 7: //ppuData

            Ppu->bus[Ppu->address.complete] = data;

            if(Ppu->control.vramIncrement) Ppu->address.complete += 32;
            else Ppu->address.complete++;

        break;
    }
}

byte ppuRead(PPU * Ppu, word address){ //send the registers to the bus so the components can read them
    
    address -= 0x2000;
    switch(address){
        case 0: //ppuctrl

            return Ppu->control.full;

        break;

        case 1: //ppumask

            return Ppu->mask.full;
            
        break;

        case 2: //ppustatus

            return Ppu->status.full;
            Ppu->status.vblank = 0;

        break;

        case 3: //oamAddress


        break;

        case 4: //oamData


        break;

        case 5: //ppuScroll


        break;

        case 6: //ppuAddr

            //makes no sense

        break;

        case 7: //ppuData

            //delayed by one cycle

        break;
    }

}

void initPpu(PPU * Ppu){
    Ppu->address.expectLsb = 0;
    Ppu->scroll.expectingY = 0;
    Ppu->control.full = 0;
    Ppu->dataByteBuffer = 0;
    Ppu->mask.full = 0;
    Ppu->status.full = 0;
}
