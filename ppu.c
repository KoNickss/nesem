#include "ppu.h"
#include "common.h"

#include "cartridge.h" //needs acces to the cartridge to load CHR maps, since r/w functions are in-house instead of bus-wide like it is for busRead/Write it needs to be imported here too, quite like there are physical wires connecting the cartridge CHR bank pins to the PPU
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "window.h"
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


bool ppuGetNmiStatus(void){
	return ppu.control.nmiVerticalBlank;
}

void ppuClearNmiStatus(void){
    ppu.control.nmiVerticalBlank = false;
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

        if(address >= 0x3000 && address <= 0x3EFF) //mirrored region of nametables
            address -= 0x1000;

        if(address >= 0x3F20 && address <= 0x3FFF) //mirrored region of palette data
            address = (address - 0x3F20) % 0x20 + 0x3F00;

        return ppuBus[address];
    }

    else return cartResponse;

}

static void ppuWrite(word address, byte data){

    if(!mapper000_Write(address, data, true)){

        if(address >= 0x3000 && address <= 0x3EFF) //mirrored region of nametables
            address -= 0x1000;

        if(address >= 0x3F20 && address <= 0x3FFF) //mirrored region of palette data
            address = (address - 0x3F20) % 0x20 + 0x3F00;

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

int getFormatColorFromPaletteRam(byte palette, byte pixel){
    word addr = 0x3F00 + (palette << 2) + pixel & 0x3F;
    byte data = ppuRead(addr);
    return ppu.PALCOL[data];
}


#define PPU_WIDTH 341
#define PPU_HEIGHT 241
void initPpu(){
    ppu.dataByteBuffer = 0;
    ppu.expectingLsb = 0;
    ppu.control.full = 0;
    ppu.dataByteBuffer = 0;
    ppu.mask.full = 0;
    ppu.status.full = 0;

    ppu.bgShift.attrHi = 0;
    ppu.bgShift.attrLo = 0;
    ppu.bgShift.patternHi = 0;
    ppu.bgShift.patternLo = 0;

    // FORMAT IS AABBGGRR, reverse of RRGGBBAA

    ppu.PALCOL[0x00] = 0xFF626262;
    ppu.PALCOL[0x01] = 0xFF902001;
    ppu.PALCOL[0x02] = 0xFFA00B24;
    ppu.PALCOL[0x03] = 0xFF900047;
    ppu.PALCOL[0x04] = 0xFF620060;
    ppu.PALCOL[0x05] = 0xFF24006A;
    ppu.PALCOL[0x06] = 0xFF001160;
    ppu.PALCOL[0x07] = 0xFF002747;
    ppu.PALCOL[0x08] = 0xFF003C24;
    ppu.PALCOL[0x09] = 0xFF004A01;
    ppu.PALCOL[0x0A] = 0xFF004F00;
    ppu.PALCOL[0x0B] = 0xFF244700;
    ppu.PALCOL[0x0C] = 0xFF623600;
    ppu.PALCOL[0x0D] = 0xFF000000;
    ppu.PALCOL[0x0E] = 0xFF000000;
    ppu.PALCOL[0x0F] = 0xFF000000;

    ppu.PALCOL[0x10] = 0xFFababab;
    ppu.PALCOL[0x11] = 0xFFe1561f;
    ppu.PALCOL[0x12] = 0xFFff394d;
    ppu.PALCOL[0x13] = 0xFFef237e;
    ppu.PALCOL[0x14] = 0xFFb71ba3;
    ppu.PALCOL[0x15] = 0xFF6422b4;
    ppu.PALCOL[0x16] = 0xFF0e37ac;
    ppu.PALCOL[0x17] = 0xFF00558c;
    ppu.PALCOL[0x18] = 0xFF00725e;
    ppu.PALCOL[0x19] = 0xFF00882d;
    ppu.PALCOL[0x1A] = 0xFF009007;
    ppu.PALCOL[0x1B] = 0xFF478900;
    ppu.PALCOL[0x1C] = 0xFF9d7300;
    ppu.PALCOL[0x1D] = 0xFF000000;
    ppu.PALCOL[0x1E] = 0xFF000000;
    ppu.PALCOL[0x1F] = 0xFF000000;

    ppu.PALCOL[0x20] = 0xFFFFFFFF;
    ppu.PALCOL[0x21] = 0xFFFFAC67;
    ppu.PALCOL[0x22] = 0xFFFF8D95;
    ppu.PALCOL[0x23] = 0xFFFF7508;
    ppu.PALCOL[0x24] = 0xFFFF6AF2;
    ppu.PALCOL[0x25] = 0xFFC56FFF;
    ppu.PALCOL[0x26] = 0xFF6A83FF;
    ppu.PALCOL[0x27] = 0xFF1FA9E6;
    ppu.PALCOL[0x28] = 0xFF00BFB8;
    ppu.PALCOL[0x29] = 0xFF01D885;
    ppu.PALCOL[0x2A] = 0xFF35E35B;
    ppu.PALCOL[0x2B] = 0xFF88DE45;
    ppu.PALCOL[0x2C] = 0xFFE3CA49;
    ppu.PALCOL[0x2D] = 0xFF4E4E4E;
    ppu.PALCOL[0x2E] = 0xFF000000;
    ppu.PALCOL[0x2F] = 0xFF000000;

    ppu.PALCOL[0x30] = 0xFFffffff;
    ppu.PALCOL[0x31] = 0xFFffe0bf;
    ppu.PALCOL[0x32] = 0xFFffd3d1;
    ppu.PALCOL[0x33] = 0xFFffc9e6;
    ppu.PALCOL[0x34] = 0xFFffc3f7;
    ppu.PALCOL[0x35] = 0xFFeec4ff;
    ppu.PALCOL[0x36] = 0xFFc9cbff;
    ppu.PALCOL[0x37] = 0xFFa9d7f7;
    ppu.PALCOL[0x38] = 0xFF97e3e6;
    ppu.PALCOL[0x39] = 0xFF97eed1;
    ppu.PALCOL[0x3A] = 0xFFa9f3bf;
    ppu.PALCOL[0x3B] = 0xFFc9f2b5;
    ppu.PALCOL[0x3C] = 0xFFeeebb5;
    ppu.PALCOL[0x3D] = 0xFFb8b8b8;
    ppu.PALCOL[0x3E] = 0xFF000000;
    ppu.PALCOL[0x3F] = 0xFF000000;



    sterlize_ppu();

    window_init(PPU_WIDTH, PPU_HEIGHT);

    //pthread_create(&ppuThread_id, NULL, ppuThread, NULL);
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

static unsigned int img_data[PPU_WIDTH * PPU_HEIGHT];
static word crt_x = 0;


#define NORMAL_PUTROW 1
#define FORCE_NAMETABLE_DRAW 1

#define ppuPutTile_getHighestBit(val) ((val >> 7) & 0b1)


static void ppuPutTileRow(){
    /*
#if !NORMAL_PUTROW
	byte selected_color;
    int tile_offset = 0;
    #if !FORCE_NAMETABLE_DRAW
    for(int tile_num = 0; tile_num < 32*32; tile_num++){
        for(int y = 0; y < 8; y++){
            bgLSBbuf = ppuRead(y + (tile_num + tile_offset)*16);
            bgMSBbuf = ppuRead(y + 8 + (tile_num+tile_offset)*16);
            for(int x = 0; x < 8; x++){
                selected_color = ppuPutTile_getHighestBit(bgLSBbuf);
                selected_color |= ppuPutTile_getHighestBit(bgMSBbuf) << 1;

                unsigned long selected_pixel = PPU_WIDTH * scanline + crt_x + x;
                selected_pixel = PPU_WIDTH * (y + ((tile_num/32)*8)) + x + ((tile_num%32) * 8);
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
    */

    //NORMAL EXECUTION

    for(int i = 0; i < 0x300; i++){

        int tile_num = ppuRead(0x2000 + i);

        for(int y = 0; y < 8; y++){

            //bgLSBbuf = ppuRead(y + (tile_num + tile_offset)*16);
            //bgMSBbuf = ppuRead(y + 8 + (tile_num+tile_offset)*16);

            for(int x = 0; x < 8; x++){

                byte bgPixel = 0;
                byte bgPal = 0;

                if(ppu.mask.showBackdropDebug){
                    word bit_m = 0x8000 >> ppu.xReg;

                    byte pixelLo = (ppu.bgShift.patternLo & bit_m) > 0;
                    byte pixelHi = (ppu.bgShift.patternHi & bit_m) > 0;
                    bgPixel = (pixelHi << 1) | pixelLo;

                    byte palLo = (ppu.bgShift.attrLo & bit_m) > 0;
                    byte palHi = (ppu.bgShift.attrHi & bit_m) > 0;
                    bgPal = (palHi << 1) | palLo;
                }


                int color = getFormatColorFromPaletteRam(bgPal, bgPixel);
                unsigned long selected_pixel = PPU_WIDTH * scanline + crt_x + x;
                selected_pixel = PPU_WIDTH * (y+((i/32)*8)) + x + ((i%32) * 8);

                if(selected_pixel >= PPU_WIDTH*PPU_HEIGHT){
                    fprintf(stderr, "WARN: Out of bounds PPU access\n");
                }else{

                    img_data[selected_pixel] = color;
                }


                //bgLSBbuf <<= 1;
                //bgMSBbuf <<= 1;
            }
        }
    }
    crt_x += 8;
    /*
    #endif
#else
	byte selected_color;
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

#endif*/
}




void loadBackShifters(){
    ppu.bgShift.patternLo = (ppu.bgShift.patternLo & 0xFF00) | (bgLSBbuf); // LOAD incoming data at the bottom of the 16 bit shift register
    ppu.bgShift.patternHi = (ppu.bgShift.patternHi & 0xFF00) | (bgMSBbuf); // --||--

    if(ATbuf & 0b01){
        ppu.bgShift.attrLo = (ppu.bgShift.attrLo & 0xFF00) | 0xFF; //set lower 8 bits to the LSB of the next tileATTR to make sure patterns and pallettes are in sync
    }else{
        ppu.bgShift.attrLo = (ppu.bgShift.attrLo & 0xFF00) | 0x00; //set lower 8 bits to the LSB of the next tileATTR to make sure patterns and pallettes are in sync
    }

    if(ATbuf & 0b10){
        ppu.bgShift.attrHi = (ppu.bgShift.attrHi & 0xFF00) | 0xFF; //set lower 8 bits to the LSB of the next tileATTR to make sure patterns and pallettes are in sync
    }else{
        ppu.bgShift.attrHi = (ppu.bgShift.attrHi & 0xFF00) | 0x00; //set lower 8 bits to the LSB of the next tileATTR to make sure patterns and pallettes are in sync
    }
}


void updateShifters(){
    if(ppu.mask.showBackdropDebug | ppu.mask.showSpritesDebug){
        ppu.bgShift.patternLo <<= 1;
        ppu.bgShift.patternHi <<= 1;
        ppu.bgShift.attrLo <<= 1;
        ppu.bgShift.attrHi <<= 1;
    }
}




//TODO: maybe make nametable id a union

void incrementScrollX_Routine(){
    if(ppu.mask.showBackdropDebug || ppu.mask.showSpritesDebug){ //if rendering

        if(ppu.vReg.field.coarseX == 31){ // if leaving nametable
            ppu.vReg.field.coarseX = 0; //loop back around
            ppu.vReg.field.nameTableID = (ppu.vReg.field.nameTableID & 0b10) | ~(ppu.vReg.field.nameTableID & 0b01); // FLIP NAMETABLE X BIT
        }else{
            ppu.vReg.field.coarseX++;
        }

    }
}

void incrementScrollY_Routine(){
    if(ppu.mask.showBackdropDebug || ppu.mask.showSpritesDebug){ //if rendering

        if(ppu.vReg.field.fineY < 7){
            ppu.vReg.field.fineY++;
        }else{
            ppu.vReg.field.fineY = 0; //the increment coarseY correspondent to this is at the end of the if gates

            if(ppu.vReg.field.coarseY == 29){ //we need to swap vertical NT targets
                ppu.vReg.field.coarseY = 0;
                ppu.vReg.field.nameTableID = ~(ppu.vReg.field.nameTableID & 0b10) | (ppu.vReg.field.nameTableID & 0b01);
            }else if(ppu.vReg.field.coarseY == 31){ // in case the pointer gets in attribute memory
                ppu.vReg.field.coarseY = 0;
            }else{

                ppu.vReg.field.coarseY++;

            }
        }
    }
}

void resetAddressX_Routine(){ // IMPORTANT V SYNC
    if(ppu.mask.showBackdropDebug || ppu.mask.showBackdropDebug){ //if rendering
        ppu.vReg.field.nameTableID = (ppu.vReg.field.nameTableID & 0b10) | (ppu.tReg.field.nameTableID & 0b01); //GET NT_X from tREG
        ppu.vReg.field.coarseX = ppu.tReg.field.coarseX;
    }
}

void resetAddressY_Routine(){ // IMPORTANT V SYNC
    if(ppu.mask.showBackdropDebug || ppu.mask.showSpritesDebug){ //if rendering
        ppu.vReg.field.fineY = ppu.tReg.field.fineY;
        ppu.vReg.field.nameTableID = (ppu.tReg.field.nameTableID & 0b10) | (ppu.vReg.field.nameTableID & 0b01);
    }
}


void ppuClock(void){
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

            updateShifters();

            switch((cycle - 1) % 8){

                case 0:
                    //READ NameTable BYTE
                    loadBackShifters();
                    NTbuf = ppuBus[resolveNameTableAddress(ppu.vReg.data)];
                break;


                case 2:
                    //READ AttributeTable BYTE
                    ATbuf = ppuBus[resolveAttributeTableAddress(ppu.vReg.data)];
                break;


                case 4:
                    //READ bg lsb
                    bgLSBbuf = ppuRead((ppu.control.backgroundPatternTable << 12) + (word)(NTbuf << 4) + ppu.vReg.field.fineY + 0);
                break;



                case 6:
                    bgMSBbuf = ppuRead((ppu.control.backgroundPatternTable << 12) + (word)(NTbuf << 4) + ppu.vReg.field.fineY + 8);
                    #if NORMAL_PUTROW
                        //ppuPutTileRow();
                    #endif
                break;

                case 7:
                    incrementScrollX_Routine();
                break;

                default:
                    //In-between cycles
                break;
            }
        }
    }

    if(cycle == 256){ //done with visible row
        incrementScrollY_Routine();
    }

    if(cycle == 257){
        resetAddressX_Routine(); //incrementing Y means our X is now incorrect and needs resetting
    }

    if(scanline == 0 && cycle >= 280 && cycle < 305){
        resetAddressY_Routine();
    }


    //SCANLINE over and reset counters
    if(cycle >= 341){
        cycle -= 341;
        scanline++;
        crt_x = 0;
    }

    if(scanline == 241 && cycle == 0){
        ppu.control.nmiVerticalBlank = true;
        ppu.status.vblank = true;
        crt_x = 0;
        #if !NORMAL_PUTROW
            //ppuPutTileRow();
        #endif
        //stbi_write_png("ppu.png", PPU_WIDTH, PPU_HEIGHT, 4, img_data, PPU_WIDTH * 4);
        window_update_image(PPU_WIDTH, PPU_HEIGHT, (void*)img_data);
    }

    if(scanline >= 262){
        scanline = 0;
        crt_x = 0;
        ppu.status.vblank = false;
        ppu.control.nmiVerticalBlank = false;
    }

    byte bgPixel = 0;
    byte bgPal = 0;

    if(ppu.mask.showBackdropDebug){
        word bit_m = 0x8000 >> ppu.xReg;

        byte pixelLo = (ppu.bgShift.patternLo & bit_m) > 0;
        byte pixelHi = (ppu.bgShift.patternHi & bit_m) > 0;
        bgPixel = (pixelHi << 1) | pixelLo;

        byte palLo = (ppu.bgShift.attrLo & bit_m) > 0;
        byte palHi = (ppu.bgShift.attrHi & bit_m) > 0;
        bgPal = (palHi << 1) | palLo;
    }

    printf("\n%#12x | %#12x %#12x | %#12x | %d\n", ppu.vReg.field.coarseX, ppu.vReg.field.fineY, ppu.vReg.field.coarseY, ppu.vReg.field.nameTableID, scanline);


    int color = getFormatColorFromPaletteRam(bgPal, bgPixel);
    unsigned long selected_pixel = PPU_WIDTH * scanline + cycle - 1;

    if(selected_pixel >= PPU_WIDTH*PPU_HEIGHT){
        //fprintf(stderr, "WARN: Out of bounds PPU access\n");
    }else{

        img_data[selected_pixel] = color;
    }

	cycle++;
}

//For if we ever want the ppu to run on a different thread
static void* ppuThread(void* args){

	while(true){
		ppuClock();
	}

	return args;
}
