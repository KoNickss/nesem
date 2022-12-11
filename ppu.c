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
            ppu.tReg.bits.nameTableID = ppu.control.nameTableID;

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

            if(ppu.expectingLsb){
                //second write
                ppu.tReg.bits.fineY = data & 0b00000111; //set fineY
                ppu.tReg.bits.coarseY = data >> 3; //set coarse Y
            }else{
                //first write
                ppu.xReg = data & 0b00000111; //set fineX
                ppu.tReg.bits.coarseX = data >> 3; //set coarse X
            }

            ppu.expectingLsb = !ppu.expectingLsb;

        break;

        case 6: //ppuAddr

            if(ppu.expectingLsb){
                //second write
                ppu.tReg.data = (ppu.tReg.data & 0xFF00) | (data & 0x00FF);

                ppu.vReg.data = ppu.tReg.data; //IMPORTANT V SYNC

            }else{
                //first write
                word buf;
                buf = data & 0b00111111; //eliminate highest 2 bits
                buf = buf << 8;
                buf = buf | (ppu.tReg.data & 0xFF);
                ppu.tReg.data = buf;

                ppu.tReg.bits.fineY = ppu.tReg.bits.fineY & 0b011; //clear highest bit of fine Y
            }

            ppu.expectingLsb = !ppu.expectingLsb;

        break;

        case 7: //ppuData

            ppuWrite(ppu.vReg.data, data);

            if(ppu.control.vramIncrement) ppu.vReg.data += 32;
            else ppu.vReg.data++;

        break;

        default:
            fprintf(stderr, "ERR: Invalid write to PPU register of address %llX\n!", address + 0x2000);
            abort();
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
            ppu.expectingLsb = false;

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

            ppu.dataByteBuffer = ppuRead(ppu.vReg.data);

            if(ppu.vReg.data >= 0x3F00 && 0x3FFF <= ppu.vReg.data) //except when reading palette info
                returnData = ppuRead(ppu.vReg.data); //then the query is immediate


            return returnData;

        break;

        default:
            fprintf(stderr, "ERR: Invalid read to PPU register of address %llX\n!", address + 0x2000);
            abort();
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
    ppu.expectingLsb = 0;
    ppu.control.full = 0;
    ppu.dataByteBuffer = 0;
    ppu.mask.full = 0;
    ppu.status.full = 0;

    ppu.vReg.bits.fixedOne = 1;
    ppu.tReg.bits.fixedOne = 1;
}

void ppuClock(){
    //one tick of the PPU clock

    

}