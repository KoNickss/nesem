CC ?= cc
CFLAGS = -Ofast -ffat-lto-objects -flto -fuse-linker-plugin -fno-stack-protector
CFLAGS_EX ?=
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

CFLAGS += $(CFLAGS_EX)


default: all



$(ODIR)/%.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $*.c

all: $(addprefix $(ODIR)/, $(C_FILES:.c=.o)) | $(ODIR)
	@echo "Linking..."
	@$(CC) $(CFLAGS) $(O_FILES) $(LIBS) -o $(OUTFILE)

clean:
	$(RM) $(OUTFILE)
	$(RM) $(O_FILES)
	$(RM) $(ODIR)
debug:
	@$(MAKE) all DEBUG=true --no-print-directory

debug-tickonclick:
	@$(MAKE) all TICK_ON_KEY=true DEBUG=true --no-print-directory

$(ODIR):
	@mkdir -p $@
