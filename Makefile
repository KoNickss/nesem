 CC = cc
CFLAGS = -static
CDEBUGFLAGS = -g -DDEBUG -static
LIBS=-lm -pthread
RM = rm -rf
OUTFILE = nesem


default: all

all:
	$(CC) $(CFLAGS) bus.c cpu.c cartridge.c ppu.c $(LIBS) -o $(OUTFILE)

clean:
	$(RM) $(OUTFILE)
	
debug:
	$(CC) $(CDEBUGFLAGS) bus.c cpu.c ppu.c cartridge.c $(LIBS) -o $(OUTFILE)

debug-tickonclick:
	$(CC) $(CDEBUGFLAGS) -DTICKONKEY bus.c cpu.c ppu.c $(LIBS) cartridge.c -o $(OUTFILE)
