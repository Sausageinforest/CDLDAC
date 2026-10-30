.SUFFIXES:

ifeq ($(strip $(PS3DEV)),)
export PS3DEV := /usr/local/ps3dev
endif
ifeq ($(strip $(PSL1GHT)),)
export PSL1GHT := $(PS3DEV)
endif

TITLE       := PS3 CD-DA Streamer
APPID       := CDLDAC001
CONTENTID   := UP0001-$(APPID)_00-0000000000000000
ICON0       := $(CURDIR)/ICON0.PNG

include $(PSL1GHT)/ppu_rules

TARGET      := CDLDAC001
OBJS        := source/cdrom.o source/main.o

CFLAGS      = -O2 -Wall -mcpu=cell $(MACHDEP) -Isource $(LIBPSL1GHT_INC)
LDFLAGS     = $(MACHDEP)
LIBS        := -lio -lnet -lsysutil -lsysmodule -llv2

.PHONY: all pkg clean

all: $(TARGET).self
pkg: $(TARGET).pkg

source/%.o: source/%.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	@echo "LD $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBPSL1GHT_LIB) $(LIBS) -o $@

clean:
	@rm -rf source/*.o $(TARGET).elf $(TARGET).self $(TARGET).pkg build
