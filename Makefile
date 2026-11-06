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

OBJS        := $(SRCS:.c=.o)
INCLUDES    := -Iinclude -Isrc

# Check if compiling under PS3 environment
ifneq ($(wildcard $(PSL1GHT)/ppu_rules),)
# ------------------------------------------------------------------------------
# PS3 Toolchain Environment
# ------------------------------------------------------------------------------
include $(PSL1GHT)/ppu_rules

CFLAGS      := -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDES) $(LIBPSL1GHT_INC)
LDFLAGS     := $(MACHDEP)
LIBS        := -laudio -lio -lnet -lsysutil -lsysmodule -llv2 -lm

# Detect Tiny3D if available
ifneq ($(wildcard $(PS3DEV)/portlibs/ppu/lib/libtiny3d.a $(PSL1GHT)/lib/libtiny3d.a),)
LIBS        += -ltiny3d
endif

.PHONY: all pkg clean test host

all: $(TARGET).self
pkg: $(TARGET).pkg

src/%.o: src/%.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	@echo "LD $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBPSL1GHT_LIB) $(LIBS) -o $@

else
# ------------------------------------------------------------------------------
# Host Environment (Testing / Verification)
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
	rm -rf *.o src/*.o build $(TARGET).elf $(TARGET).self $(TARGET).pkg ps3-cd-player-host run_tests cache
