#include "ppu.h"

#include "cartridge.h" //needs acces to the cartridge to load CHR maps, since r/w functions are in-house instead of bus-wide like it is for busRead/Write it needs to be imported here too, quite like there are physical wires connecting the cartridge CHR bank pins to the PPU

byte ppuBus[0x3FFF];

PPU ppu;


byte ppuRead(word address){

    word cartResponse;

    if((cartResponse = mapper000_Read(address, true)) == 0x0100){

        if(address >= 0x3000 && 0x3EFF <= address) //mirrored region
            address -= 0x1000;

        if(address >= 0x3F20 && 0x3FFF <= address) //mirrored region
            address = (address - 0x3F00) % 0x20 + 0x3F00;

        return ppuBus[address];
    }

    else return cartResponse;

}

void ppuWrite(word address, byte data){

    if(!mapper000_Write(address, data, true)){

        if(address >= 0x3000 && 0x3EFF <= address) //mirrored region
            address -= 0x1000;

        if(address >= 0x3F20 && 0x3FFF <= address) //mirrored region
            address = (address - 0x3F00) % 0x20 + 0x3F00;

        ppuBus[address] = data;
    }

}


void ppuRegWrite(word address, byte data){
    address -= 0x2000;
    switch(address){
        case 0: //ppuctrl

            ppu.control.full = data;

        break;

        case 1: //ppumask

            ppu.mask.full = data;

        break;

        case 2: //ppustatus

            ppu.status.full = data;

        break;

        case 3: //oamAddress

        break;

        case 4: //oamData

        break;

        case 5: //ppuScroll

            if(ppu.scroll.expectingY){
                ppu.scroll.y = data;
            }else{
                ppu.scroll.x = data;
            }

            ppu.scroll.expectingY = !ppu.scroll.expectingY;

        break;

        case 6: //ppuAddr

            if(ppu.address.expectLsb){
                ppu.address.lsb = data;
            }else{
                ppu.address.msb = data;
            }

            ppu.address.expectLsb = !ppu.address.expectLsb;

        break;

        case 7: //ppuData

            ppuWrite(ppu.address.complete, data);

            if(ppu.control.vramIncrement) ppu.address.complete += 32;
            else ppu.address.complete++;

        break;
    }
}

byte ppuRegRead(word address){ //send the registers to the bus so the components can read them
    
    address -= 0x2000;

    byte returnData;

    switch(address){
        case 0: //ppuctrl

            return ppu.control.full;

        break;

        case 1: //ppumask

            return ppu.mask.full;
            
        break;

        case 2: //ppustatus

            return ppu.status.full;
            ppu.status.vblank = 0;
            ppu.address.expectLsb = false;

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

            returnData = ppu.dataByteBuffer; //reads are lagged back by one cycle, the data you read is the data of the last query

            ppu.dataByteBuffer = ppuRead(ppu.address.complete);

            if(ppu.address.complete >= 0x3F00 && 0x3FFF <= ppu.address.complete) //except when reading palette info
                returnData = ppuRead(ppu.address.complete); //then the query is immediate


            return returnData;

        break;
    }

}

void dumpPpuBus(){
    
    for(int i = 0; i < 0x3FFF; i++){
        printf("%02X ", ppuRead(i));
    }

}

void initPpu(){
    ppu.dataByteBuffer = 0;
    ppu.address.expectLsb = 0;
    ppu.scroll.expectingY = 0;
    ppu.control.full = 0;
    ppu.dataByteBuffer = 0;
    ppu.mask.full = 0;
    ppu.status.full = 0;
}

void ppuClock(){
    //one tick of the PPU clock

    

}