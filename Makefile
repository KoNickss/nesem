CC = clang
CFLAGS = -g -DDEBUG -static
RM = rm -rf
OUTFILE = exec


default: all

all:
	$(CC) bus.c cpu.c cartridge.c -o $(OUTFILE)

clean:
	$(RM) $(OUTFILE)
	
debug:
	$(CC) $(CFLAGS) bus.c cpu.c ppu.c cartridge.c -o $(OUTFILE)
