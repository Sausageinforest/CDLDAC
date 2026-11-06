.SUFFIXES:

ifeq ($(strip $(PS3DEV)),)
export PS3DEV := /usr/local/ps3dev
endif
ifeq ($(strip $(PSL1GHT)),)
export PSL1GHT := $(PS3DEV)
endif

TITLE       := PS3 Audiophile CD Player
APPID       := CDPL00001
CONTENTID   := UP0001-$(APPID)_00-0000000000000000
ICON0       := $(CURDIR)/pkg/ICON0.PNG

-include $(PSL1GHT)/ppu_rules

TARGET      := CDPL00001
SRCS        := src/main.c \
               src/cd_drive.c \
               src/discid.c \
               src/metadata.c \
               src/audio_engine.c \
               src/visualizer.c \
               src/ui.c \
               src/input.c \
               src/cJSON.c

INCLUDES    := -Iinclude -Isrc

ifneq ($(wildcard $(PS3DEV)/bin/powerpc-ps3-elf-gcc),)
# ------------------------------------------------------------------------------
# PS3 PSL1GHT Toolchain Target
# ------------------------------------------------------------------------------
CC          := powerpc-ps3-elf-gcc
CFLAGS      := -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDES) $(LIBPSL1GHT_INC)
LDFLAGS     := $(MACHDEP)
LIBS        := $(LIBPSL1GHT_LIB) -laudio -lio -lnet -lsysutil -lsysmodule -llv2 -lm

# Detect Tiny3D if available
ifneq ($(wildcard $(PS3DEV)/portlibs/ppu/include/tiny3d.h $(PSL1GHT)/include/tiny3d.h),)
LIBS        += -ltiny3d
CFLAGS      += -DHAVE_TINY3D
endif

OBJS        := $(SRCS:.c=.ppu.o)

.PHONY: all pkg clean test host

all: $(TARGET).self
pkg: $(TARGET).pkg

%.ppu.o: %.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	@echo "LD $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

else
# ------------------------------------------------------------------------------
# Host Target for Testing / Verification
# ------------------------------------------------------------------------------
CC          := clang
HOST_CFLAGS := -O2 -Wall $(INCLUDES)
HOST_LDFLAGS:= -lm -lpthread

all: test host
endif

host: $(SRCS)
	$(CC) $(HOST_CFLAGS) $(SRCS) $(HOST_LDFLAGS) -o ps3-cd-player-host
	@echo "Built ps3-cd-player-host successfully."

test: tests/test_suite.c src/cd_drive.c src/discid.c src/metadata.c src/visualizer.c src/cJSON.c
	$(CC) $(HOST_CFLAGS) tests/test_suite.c src/cd_drive.c src/discid.c src/metadata.c src/visualizer.c src/cJSON.c $(HOST_LDFLAGS) -o run_tests
	@./run_tests

clean:
	rm -rf *.o src/*.o src/*.ppu.o build $(TARGET).elf $(TARGET).self $(TARGET).pkg ps3-cd-player-host run_tests cache
