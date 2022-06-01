CC = clang
CFLAGS = -g -DDEBUG -static
RM = rm -rf
OUTFILE = exec


default: all

all: $(OUTFILE)
	$(CC) bus.c cpu.c cartridge.c -o $(OUTFILE)

clean:
	$(RM) exec
	
debug $(OUTFILE):
	$(CC) $(CFLAGS) bus.c cpu.c cartridge.c -o $(OUTFILE)
