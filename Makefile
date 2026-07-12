CC = cc
CFLAGS = -Ofast
CDEBUGFLAGS = -g -DDEBUG -g
LIBS=-lm -pthread -lX11 -lxcb
RM = rm -rf
OUTFILE = nesem


ODIR ?= build

TICK_ON_KEY ?= false
DEBUG ?= false

C_FILES ?= bus.c cpu.c cartridge.c ppu.c window.c controller.c joystick.c sound.c
O_FILES = $(abspath $(addprefix $(ODIR)/, $(C_FILES:.c=.o)))


ifeq ($(TICK_ON_KEY), true)
	CFLAGS += -DTICKONKEY
endif

ifeq ($(DEBUG), true)
	CFLAGS = $(CDEBUGFLAGS)
endif


default: all



$(ODIR)/%.o : %.c miniaudio.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $*.c

all: $(addprefix $(ODIR)/, $(C_FILES:.c=.o)) | $(ODIR)
	@echo "Linking..."
	@$(CC) $(CFLAGS) $(O_FILES) $(LIBS) -o $(OUTFILE)

clean:
	$(RM) $(OUTFILE)
	$(RM) miniaudio.h
	$(RM) $(O_FILES)
	$(RM) $(ODIR)
debug:
	@$(MAKE) all DEBUG=true --no-print-directory

debug-tickonclick:
	@$(MAKE) all TICK_ON_KEY=true DEBUG=true --no-print-directory

miniaudio.h:
	curl -L -O https://raw.githubusercontent.com/mackron/miniaudio/refs/heads/master/miniaudio.h

$(ODIR):
	@mkdir -p $@
