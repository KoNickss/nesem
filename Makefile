CC = cc
CFLAGS = -Ofast
CDEBUGFLAGS = -g -DDEBUG -g
LIBS=-lm -pthread -lX11 -lxcb
RM = rm -rf
OUTFILE = nesem


default: all

all:
	$(CC) $(CFLAGS) bus.c cpu.c cartridge.c ppu.c window.c controller.c joystick.c sound.c $(LIBS) -o $(OUTFILE)

clean:
	$(RM) $(OUTFILE)
	
debug:
	$(CC) $(CDEBUGFLAGS) bus.c cpu.c ppu.c cartridge.c window.c controller.c joystick.c sound.c $(LIBS) -o $(OUTFILE)

debug-tickonclick:
	$(CC) $(CDEBUGFLAGS) -DTICKONKEY bus.c cpu.c ppu.c window.c controller.c $(LIBS) cartridge.c sound.c -o $(OUTFILE)

download_miniaudio:
	wget https://raw.githubusercontent.com/mackron/miniaudio/refs/heads/master/miniaudio.h