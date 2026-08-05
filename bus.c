#include "bus.h"
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

//For displaying to the screen
#include "window.h"

//For soundcard
#define BUS_C
#include "sound.h"


unsigned char bus[BUS_SIZE]; //this is the bus array

//Importing the different electrical components of the NES as definitions, not yet docked to the bus

#include "cpu.h"

#include "ppu.h"

#include "apu.h"

#include "cartridge.h"

#include "controller.h"

#include "window.h"

#define TRANSLATE_PPU_ADDRESS(address) ((address - PPU_START) % 8 + PPU_START)

static CPU* cpu = NULL;


void performOamDMA(byte pageID){
    word startAddr = pageID << 8;

    byte page[256];

    cpuConsumeCycle(cpu);
    cpuConsumeCycle(cpu);
    cpu->extraCycles += 2;
    for(word i = 0; i < 256; ++i){
        cpu->extraCycles += 2;
        page[i] = busRead8(startAddr + i);
        cpuConsumeCycle(cpu); //Consume cycle for the write. Writes are handled by ppuSwallowOAMDMA so we handle acking the clock here. This is not proper emulation but it works okay
    }

    ppuSwallowOAMDMA(page);
}

void busWrite8(word address, byte data){
    cpuConsumeCycle(cpu);

    if(cart_PRG_Write(address, data)){ //first thing we do is we hand the operation to the mapper to resolve any cartridge-side bank switching and mirroring, if the address we wanna write to isnt on the cartridge, we return false and we write to the bus normally
        return;
    }else{
        if(PPU_START <= address && address <= PPU_END){
            address = TRANSLATE_PPU_ADDRESS(address);
            ppuRegWrite(address, data & 0xFF);
            return;
        }

        if(address == JOYPAD1_ADDR){
            if((data & 0b1) == 0b1){
                joypad_prepare_read();
            }else if((data & 0b1) == 0b0){
                joypad_publish_state();
            }
            return;
        }
        if(address <= 0x1FFF){ //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address &= 0b11111111111;
            bus[address] = (byte)data;
            return;
        }

        if(address == OAM_DMA_ADDR){
            //STOP!! TRIGGER DIRECT MEMORY ACCESS: copy one entire page of cpu memory to the PPU's OAM (sprite state sheet), as manually doing this usind PPU oam data/addr regs would be slow. Almost all roms but the most primitive use this.

            performOamDMA((byte)data); //because DMAs *usually* happen during vblank, we can get away with an instant copy and just bill the cycle cost to the cpu artificially, but to be 100% accurate we could paint it in as one by one, but that seems useless for the most part

            return;

        }

        if(address >= 0x4000 && address <= 0x4017)
            apuRegWrite(address - 0x4000, data);

        DWARN("Could not write to address 0x%X! Open Bus Write!", address);
    }
}


//Do a read without affecting PPU
static byte _busRead8NoCycle(word address){
    static byte data = 0;
    //we first ask the mapper to read the data from the address for us in case its on the cartridge, if it returns false that means the data stored at that address is not on the cartridge, but rather on the nes memory, thus we hand the job over to the bus
    bool valid_cart_response = cart_PRG_Read(address, &data);
    if(valid_cart_response){
        return data;
    }else{


        if(address <= 0x1FFF){ //a lot of regions on the NES bus are mirrored/synced, this just ensures we are always writing to the parent region, not to a empty cloned one
            address &= 0b11111111111;
            data = bus[address];
            return data;
        }


        if(PPU_START <= address && address <= PPU_END){
            address = TRANSLATE_PPU_ADDRESS(address);
            data = ppuRegRead(address);
            return data;
        }

        if(address == JOYPAD1_ADDR){
            byte joypad_read = joypad_read_bit(JOYPAD_1);
            byte joypad_mask = 0b1111; //Only affects lower 4 bits
            data = (data & ~joypad_mask) | (joypad_read & joypad_mask); //Keep the upper 4 bits of last read byte

            return data;
        }

        if(address >= 0x4000 && address <= 0x4017)
            return apuRegRead(address - 0x4000);

        DWARN("Open bus read 0x%X", address);
        return data;
    }
}

byte busRead8(word address){
    cpuConsumeCycle(cpu);

    return _busRead8NoCycle(address);
}

word busRead16(word address){
    word d = busRead8(address); //read lsb
    d |= busRead8(address+1) << 8; //read msb
    return d; //return whole word
}

void busWrite16(word address, word data){
    busWrite8(address, data >> 8); //write msb
    busWrite8(address + 1, data & 0b11111111); //write lsb
}

void dumpBus(){
    FILE *fdump;
    fdump = fopen("dumpfile", "w");

    SMART_ASSERT(fdump != NULL, "ERR: Could not access file! Do you have permissions to create a file?\n");

    //If the file was successfuly opened then this code will run
    for(unsigned long long i = 0; i <= 0xFFFF; i++)
        fprintf(fdump, "%c", busRead8(i));

    fclose(fdump);
}

byte debug_read_do_not_use_pls(word address){
    return bus[address];
}


static inline void debug_print_instruction(CPU* __restrict__ cpu, byte opcode){
    handleErrors(cpu);

    #ifdef DEBUG
    return;
        printf("opcode: %02X\tSP: %02X\t--name: %s  address: %04X    %d %p\n",
            opcode,
            cpu->SP,
            cpu->opcodes[opcode].name,
            cpu->PC,
            cpu->opcodes[opcode].bytes,
            cpu->opcodes[opcode].microcode
        );
    #endif
}




static pthread_mutex_t _shutdown_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _shutdown_signal_cond;
void shutdown_program(void){
    pthread_mutex_lock(&_shutdown_signal_mutex);
    pthread_cond_broadcast(&_shutdown_signal_cond);
    pthread_mutex_unlock(&_shutdown_signal_mutex);
}

#define ROM_TEST_NAME ("nestest.nes")

//#include <stdatomic.h>
#define SOUNDCARD_DISABLED_STATE -1
//static atomic_llong _soundcard_frames_requested = 0;
static long long _soundcard_frames_requested = 0;



void run_nes_for_x_audio_frames(size_t frame_count){
    _soundcard_frames_requested += frame_count;

    while(_soundcard_frames_requested > 0 || _soundcard_frames_requested == SOUNDCARD_DISABLED_STATE){
        word last_pc = cpu->PC;
        int cpuCycles = cpuClock(cpu);

        SMART_ASSERT(cpuCycles == cpu->cycles_consumed_this_clock, "WONG CYCLE COUNT PC: %X, OP: %X, Real%i, Bad%i\n", last_pc, cpuCycles, _busRead8NoCycle(last_pc), cpu->cycles_consumed_this_clock);
    }

    if(window_shutdown_triggered()){
        shutdown_program();
        return;
    }
}

void write_audio_frames_to_soundcard(float* frames, size_t frame_count){

    #ifdef DEBUG
    printf("Writing %lu frames\n", frame_count);
    #endif
    size_t frames_written = playback_write_frames(frames, frame_count);
    _soundcard_frames_requested -= frames_written;
    #ifdef DEBUG
    printf("%lu frames written\n", frames_written);
    #endif

}


static void* _run_nes(void* args){
    (void)args;
    while(true){
        //TODO: Timing
        word last_pc = cpu->PC;
        int cpuCycles = cpuClock(cpu);

        SMART_ASSERT(cpuCycles == cpu->cycles_consumed_this_clock, "WONG CYCLE COUNT PC: %X, OP: %X, Real%i, Bad%i\n", last_pc, cpuCycles, _busRead8NoCycle(last_pc), cpu->cycles_consumed_this_clock);
        if(window_shutdown_triggered()){
            shutdown_program();
            return NULL;
        }
    }

    return NULL;
}


int main(int argc, char * argv[]){
    if(argc <= 1){ //Check to see if a rom was given
        PRINT_ERROR("rom", "No Rom file Specified in Arguments");
        exit(EXIT_FAILURE);
    }

    //load the CHR and PRG banks from the .nes file (argv[1]), also loads the header for mapper construction
    initBanks(argv[1]);
    if(strstr(argv[1], ROM_TEST_NAME)){
        if(strlen(strstr(argv[1], ROM_TEST_NAME)) == strlen(ROM_TEST_NAME)){ //Load test rom at 0xC000
            romStartAddress = 0xC000;
        }
    }

    initPpu(); //Create ppu and initalize memory

    cpu = (CPU*)xmalloc(sizeof(CPU)); //create new CPU

    initCpu(cpu); //put new CPU in starting mode and dock it to the bus
    cpu->PC = romStartAddress;

    initApu();

    //Setup the mutex/cond for shutting everything down
    if(pthread_mutex_init(&_shutdown_signal_mutex, NULL) != 0){
        DERROR("Could not create the shutdown mutex");
        return false;
    }
    if(pthread_cond_init(&_shutdown_signal_cond, NULL) != 0){
        pthread_mutex_destroy(&_shutdown_signal_mutex);
        DERROR("Could not create the pthread cond for shutdown");
        return false;
    }

    //Create sound engine
    pthread_t t;
    bool using_audio = playback_start_audio_engine();
    if(using_audio == false){
        PRINT_ERROR("audio", "Could not start audio engine!");
        _soundcard_frames_requested = SOUNDCARD_DISABLED_STATE;
        pthread_create(&t, NULL, _run_nes, NULL);
    }


    //Wait for program to exit
    pthread_mutex_lock(&_shutdown_signal_mutex);
    pthread_cond_wait(&_shutdown_signal_cond, &_shutdown_signal_mutex);
    pthread_mutex_unlock(&_shutdown_signal_mutex);

    //Cleanup memory
    if(using_audio){
        playback_destroy_audio_engine();
    }else{
        pthread_cancel(t);
    }
    cpuDestroy(cpu);
    free(cpu);

    window_destroy();

    pthread_mutex_destroy(&_shutdown_signal_mutex);
    pthread_cond_destroy(&_shutdown_signal_cond);

    return EXIT_SUCCESS;
}
