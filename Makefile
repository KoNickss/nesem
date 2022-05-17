all:
	clang bus.c cpu.c cartridge.c -o exec

clean:
	rm exec
	
debug:
	clang -static -g -DDEBUG bus.c cpu.c cartridge.c -o exec
