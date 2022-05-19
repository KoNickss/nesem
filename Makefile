all:
	cc bus.c cpu.c cartridge.c -o exec

clean:
	rm exec
	
debug:
	cc -static -g -DDEBUG bus.c cpu.c cartridge.c -o exec
