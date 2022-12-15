CC = cc
CFLAGS = -static -Ofast
CDEBUGFLAGS = -g -DDEBUG -static
RM = rm -rf
OUTFILE = nesem


default: all

all:
	$(CC) $(CFLAGS) bus.c cpu.c cartridge.c ppu.c -o $(OUTFILE)

clean:
	$(RM) $(OUTFILE)
	
debug:
	$(CC) $(CDEBUGFLAGS) bus.c cpu.c ppu.c cartridge.c -o $(OUTFILE)
