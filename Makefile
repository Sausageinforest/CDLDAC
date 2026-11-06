# ==============================================================================
# PS3 Audiophile CD Player (CDPL00001)
# Makefile supporting both PS3 PSL1GHT SDK and Host Verification / Testing
# ==============================================================================

TARGET       := ps3-cd-player
TITLE_ID     := CDPL00001
CONTENT_ID   := UP0001-CDPL00001_00-0000000000000000
PKG_NAME     := $(TITLE_ID).pkg

# Source files
SRCS         := src/main.c \
                src/cd_drive.c \
                src/discid.c \
                src/metadata.c \
                src/audio_engine.c \
                src/visualizer.c \
                src/ui.c \
                src/input.c \
                src/cJSON.c

INCLUDES     := -Iinclude -Isrc

# Check if PS3DEV / PSL1GHT environment is present
ifneq ($(PS3DEV),)
# ------------------------------------------------------------------------------
# PS3 PSL1GHT SDK Toolchain Configuration
# ------------------------------------------------------------------------------
CC           := powerpc-ps3-elf-gcc
CXX          := powerpc-ps3-elf-g++
STRIP        := powerpc-ps3-elf-strip
MAKE_SELF    := $(PS3DEV)/bin/make_self
MAKE_FSELF   := $(PS3DEV)/bin/make_fself
MAKE_PKG     := $(PS3DEV)/bin/make_package_npdrm

PS3_CFLAGS   := -O2 -Wall -mcpu=cell -D__PS3__ -D__PPU__ $(INCLUDES) \
                -I$(PSL1GHT)/include -I$(PS3DEV)/portlibs/ppu/include
PS3_LDFLAGS  := -L$(PSL1GHT)/lib -L$(PS3DEV)/portlibs/ppu/lib \
                -ltiny3d -lfont -laudio -lio -lnet -lm -lc

OBJS         := $(SRCS:.c=.ppu.o)

all: $(PKG_NAME)

%.ppu.o: %.c
	$(CC) $(PS3_CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(PS3_LDFLAGS) -o $@

EBOOT.BIN: $(TARGET).elf
	@if [ -x "$(MAKE_SELF)" ]; then \
		$(MAKE_SELF) $(TARGET).elf EBOOT.BIN $(CONTENT_ID); \
	elif [ -x "$(MAKE_FSELF)" ]; then \
		$(MAKE_FSELF) $(TARGET).elf EBOOT.BIN; \
	else \
		echo "Warning: make_self not found, copying ELF"; \
		cp $(TARGET).elf EBOOT.BIN; \
	fi

$(PKG_NAME): EBOOT.BIN pkg/package.conf pkg/PARAM.SFO pkg/ICON0.PNG pkg/PIC1.PNG
	@mkdir -p build/USRDIR/assets build/USRDIR/cache
	@cp EBOOT.BIN build/USRDIR/
	@cp pkg/PARAM.SFO build/
	@cp pkg/ICON0.PNG build/
	@cp pkg/PIC1.PNG build/
	@cp assets/font.ttf build/USRDIR/assets/
	@cp assets/default_cover.png build/USRDIR/assets/
	@if [ -x "$(MAKE_PKG)" ]; then \
		$(MAKE_PKG) pkg/package.conf build; \
	else \
		echo "Built package directory in build/ (ready for make_package_npdrm)"; \
	fi

else
# ------------------------------------------------------------------------------
# Host System (macOS / Linux) Configuration for Local Testing
# ------------------------------------------------------------------------------
CC           := clang
HOST_CFLAGS  := -O2 -Wall $(INCLUDES)
HOST_LDFLAGS := -lm -lpthread

all: test host

endif

# ------------------------------------------------------------------------------
# Host Executable and Test Targets
# ------------------------------------------------------------------------------
host: $(SRCS)
	$(CC) $(HOST_CFLAGS) $(SRCS) $(HOST_LDFLAGS) -o $(TARGET)-host
	@echo "Built $(TARGET)-host successfully."

test: tests/test_suite.c src/cd_drive.c src/discid.c src/metadata.c src/visualizer.c src/cJSON.c
	$(CC) $(HOST_CFLAGS) tests/test_suite.c src/cd_drive.c src/discid.c src/metadata.c src/visualizer.c src/cJSON.c $(HOST_LDFLAGS) -o run_tests
	@./run_tests

clean:
	rm -rf *.o src/*.o src/*.ppu.o build $(TARGET).elf EBOOT.BIN $(PKG_NAME) $(TARGET)-host run_tests cache

.PHONY: all clean host test
