#include "ppu.h"
#include "common.h"

#include "cartridge.h" //needs acces to the cartridge to load CHR maps, since r/w functions are in-house instead of bus-wide like it is for busRead/Write it needs to be imported here too, quite like there are physical wires connecting the cartridge CHR bank pins to the PPU
#include <pthread.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static pthread_t ppuThread_id;
static void* ppuThread(void* args);


#define PPU_BUS_SIZE (0x3FFF)
static byte ppuBus[PPU_BUS_SIZE];

PPU ppu;

static inline word resolveNameTableAddress(word regData){
    return (regData & 0b0000111111111111) | (1 << (14 - 1));
}

/*
10NNYYYYYXXXXX
10100010100011  ==> $28A3
--~~-----~~~~~
^ ^ ^    ^
| | |    |
| | y=5  x=3
| |
| 3rd name table
|
fixed
*/

static word resolveAttributeTableAddress(word regData){
    locationRegister n;
    n.data = regData;
    byte trimmedCoarseX = n.field.coarseX >> 2; //trim the lowest 2 bits of coarse X
    byte trimmedCoarseY = n.field.coarseY >> 2; //trim the lowest 2 bits of coarse Y

    word ret = trimmedCoarseX | (trimmedCoarseY << 3); //YYYXXX
    ret = 0b1111 >> 6 | ret; //1111YYYXXX
    ret = n.field.nameTableID << 10 | ret; //NN1111YYYXXX
    ret = 0b10 << 12 | ret; //10NN1111YYYXXX

    return ret;
}

/*
10NN1111YYYXXX
10101111001000
--~~----~~~---
^ ^ ^   ^  ^
| | |   |  |
| | |   |  x=3>>2
| | |   |
| | |   y=5>>2
| | |
| | offset of the attribute table from a name table
| | each name table has 960 bytes, i.e. $3C0 == 1111000000
| | that's why the address has 1111 in the middle
| |
| 3rd name table
|
fixed
*/


// ----
//
//BUS ENGINE FUNCTIONS
//
// ----

static byte ppuRead(word address){

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

static void ppuWrite(word address, byte data){

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
            ppu.tReg.field.nameTableID = ppu.control.nameTableID;

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
                ppu.tReg.field.fineY = data & 0b00000111; //set fineY
                ppu.tReg.field.coarseY = data >> 3; //set coarse Y
            }else{
                //first write
                ppu.xReg = data & 0b00000111; //set fineX
                ppu.tReg.field.coarseX = data >> 3; //set coarse X
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

                ppu.tReg.field.fineY = ppu.tReg.field.fineY & 0b011; //clear highest bit of fine Y
            }

            ppu.expectingLsb = !ppu.expectingLsb;

        break;

        case 7: //ppuData

            ppuWrite(ppu.vReg.data, data);

            if(ppu.control.vramIncrement) ppu.vReg.data += 32;
            else ppu.vReg.data++;

        break;

        default:
            fprintf(stderr, "ERR: Invalid write to PPU register of address %X\n!", address + 0x2000);
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

            returnData = ppu.status.full;
            ppu.expectingLsb = false;
            return returnData;

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
            fprintf(stderr, "ERR: Invalid read to PPU register of address %X\n!", address + 0x2000);
            abort();
        break;
    }

}

void dumpPpuBus(){
    
    for(int i = 0; i < 0x3FFF; i++){
        printf("%02X ", ppuRead(i));
    }

}

//Clean up any left over memory from a reset
static void sterlize_ppu(){
    for(unsigned int i = 0; i < PPU_BUS_SIZE; i++){
        ppuBus[i] = 0x00;
    }
}

void initPpu(){
    ppu.dataByteBuffer = 0;
    ppu.expectingLsb = 0;
    ppu.control.full = 0;
    ppu.dataByteBuffer = 0;
    ppu.mask.full = 0;
    ppu.status.full = 0;
    sterlize_ppu();

    pthread_create(&ppuThread_id, NULL, ppuThread, NULL);
}

static int cycle = 0;
static int scanline = 0;

static byte NTbuf;
static byte ATbuf;
static byte bgLSBbuf;
static byte bgMSBbuf;


//Format is AABBGGRR (aka reverse of rgba)
static const int TEST_COLORS[4] = {
	0xFF000000, //Black non-transparent
	0xFF00FFFF, //Yellow non-transparent
	0xFFFF00FF, //Magenta non-transparent
	0xFFFFFF00  //Light-blue non-transparent
};

#define PPU_WIDTH 341
#define PPU_HEIGHT 241
static unsigned int img_data[PPU_WIDTH * PPU_HEIGHT];
static word crt_x = 0;

#define ppuPutTile_getHighestBit(val) ((val >> 7) & 0b1)
static void ppuPutTileRow(){
#if 0
	byte selected_color;
    int tile_offset = 32;
    for(int tile_num = 0; tile_num < 32; tile_num++){
        for(int y = 0; y < 8; y++){
            bgLSBbuf = ppuRead(y + (tile_num + tile_offset)*16);
            bgMSBbuf = ppuRead(y + 8 + (tile_num+tile_offset)*16);
            for(int x = 0; x < 8; x++){
                selected_color = ppuPutTile_getHighestBit(bgLSBbuf);
                selected_color |= ppuPutTile_getHighestBit(bgMSBbuf) << 1;

                unsigned long selected_pixel = PPU_WIDTH * scanline + crt_x + x;
                selected_pixel = PPU_WIDTH * (y) + x + ((tile_num%32) * 8);
                if(selected_pixel >= PPU_WIDTH*PPU_HEIGHT){
                    fprintf(stderr, "WARN: Out of bounds PPU access\n");
                }else{
                    img_data[selected_pixel] = TEST_COLORS[selected_color];
                }
                        
                bgLSBbuf <<= 1;
                bgMSBbuf <<= 1;
            }
        }
    }
    crt_x += 8;
#else
	byte selected_color;
    int tile_offset = 32;
    for(int x = 0; x < 8; x++){
        selected_color = ppuPutTile_getHighestBit(bgLSBbuf);
        selected_color |= ppuPutTile_getHighestBit(bgMSBbuf) << 1;
    
        unsigned long selected_pixel = PPU_WIDTH * scanline + crt_x + x;
        if(selected_pixel >= PPU_WIDTH*PPU_HEIGHT){
            fprintf(stderr, "WARN: Out of bounds PPU access\n");
        }else{
            img_data[selected_pixel] = TEST_COLORS[selected_color];
        }
                
        bgLSBbuf <<= 1;
        bgMSBbuf <<= 1;
    }
    crt_x += 8;

#endif
}

void ppuClock(){
    //one tick of the PPU clock
#if 0
    if(cycle % 8){
        if(cycle > 0){
            if(cycle - 1 % 2){
                //High Byte
            }else{
                //Low Byte
            }
        }else{
            //H-BLANK
        }
    }
#endif

    if(scanline >= 0 && scanline <= 239){
        //visible area
        if(scanline == -1 && cycle == 1)
            ppu.status.vblank = 0;
        if((cycle >= 2 && cycle <= 257) || (cycle >= 321 && cycle <= 337)){
            switch((cycle - 1) % 8){
                
                case 0:
                    //READ NameTable BYTE
                    NTbuf = ppuBus[resolveNameTableAddress(ppu.vReg.data)];
                break;
                
                
                case 2:
                    //READ AttributeTable BYTE
                    ATbuf = ppuBus[resolveAttributeTableAddress(ppu.vReg.data)];
                break;
                
                
                case 4:
                    //READ bg lsb
                    bgLSBbuf = ppuRead((ppu.control.backgroundPatternTable << 12) + (word)(NTbuf << 4) + ppu.vReg.field.fineY + 0);//ppuBus[(ppu.control.backgroundPatternTable << 12) + (word)(NTbuf << 4) + ppu.vReg.field.fineY + 0];
                break;
                
                
                
                case 6:
                    bgMSBbuf = ppuRead((ppu.control.backgroundPatternTable << 12) + (word)(NTbuf << 4) + ppu.vReg.field.fineY + 8);//ppuBus[(ppu.control.backgroundPatternTable << 12) + (word)(NTbuf << 4) + ppu.vReg.field.fineY + 8];
                    ppuPutTileRow();
                break;

                case 7:

                break;

                default:
                    //In-between cycles
                break;
            }
        }
    }


    //SCANLINE over and reset counters
    if(cycle >= 341){
        cycle -= 341;
        scanline++;
        crt_x = 0;
    }

    if(scanline == 241){
        ppu.control.nmiVerticalBlank = true;
        ppu.status.vblank = true;
        crt_x = 0;
        stbi_write_png("ppu.png", PPU_WIDTH, PPU_HEIGHT, 4, img_data, PPU_WIDTH * 4);
    }
    
    if(scanline >= 262){
        scanline = 0;
        crt_x = 0;
        ppu.status.vblank = false;
        ppu.control.nmiVerticalBlank = false;
    }

	cycle++;
}


static void* ppuThread(void* args){

	while(true){
		ppuClock();
	}
	
	return args;
}
