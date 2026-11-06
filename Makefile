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
               src/logger.c \
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

CFLAGS      := -O2 -Wall -mcpu=cell $(MACHDEP) -D__PS3__ -D__PPU__ $(INCLUDES) -I$(PS3DEV)/portlibs/ppu/include $(LIBPSL1GHT_INC)
LDFLAGS     := $(MACHDEP) -L$(PS3DEV)/portlibs/ppu/lib -L$(PSL1GHT)/lib
LIBS        := -ltiny3d -lgcm_sys -lsysutil -lsysmodule -laudio -lio -lnet -llv2 -lm

.PHONY: all pkg clean test host

all: $(TARGET).self
pkg: $(TARGET).pkg

src/%.o: src/%.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	@echo "LD $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBPSL1GHT_LIB) -Wl,--start-group $(LIBS) -Wl,--end-group -o $@

$(TARGET).self: $(TARGET).elf
	@echo "Creating SELF $@..."
	@mkdir -p build
	@ppu-strip $< -o build/$(TARGET).elf || strip $< -o build/$(TARGET).elf || cp -f $< build/$(TARGET).elf
	@which sprxlinker >/dev/null 2>&1 && sprxlinker build/$(TARGET).elf || true
	@make_self build/$(TARGET).elf $@ || true

$(TARGET).pkg: $(TARGET).elf $(TARGET).self
	@echo "Packaging $(TARGET).pkg..."
	@rm -rf build/pkg
	@mkdir -p build/pkg/USRDIR/assets build/pkg/USRDIR/cache
	@cp -f pkg/ICON0.PNG build/pkg/ICON0.PNG
	@cp -f pkg/PIC1.PNG build/pkg/PIC1.PNG
	@if [ -f pkg/sfo.xml ] && which sfo >/dev/null 2>&1; then \
		sfo -f pkg/sfo.xml build/pkg/PARAM.SFO ; \
	else \
		cp -f pkg/PARAM.SFO build/pkg/PARAM.SFO ; \
	fi
	@cp -f assets/default_cover.png build/pkg/USRDIR/assets/
	@cp -f assets/font.ttf build/pkg/USRDIR/assets/
	@mkdir -p build
	@ppu-strip $(TARGET).elf -o build/$(TARGET).elf || strip $(TARGET).elf -o build/$(TARGET).elf || cp -f $(TARGET).elf build/$(TARGET).elf
	@which sprxlinker >/dev/null 2>&1 && sprxlinker build/$(TARGET).elf || true
	@make_self_npdrm build/$(TARGET).elf build/pkg/USRDIR/EBOOT.BIN $(CONTENTID) || cp -f $(TARGET).self build/pkg/USRDIR/EBOOT.BIN
	@pkg --contentid $(CONTENTID) build/pkg/ $(TARGET).pkg
	@which package_finalize >/dev/null 2>&1 && package_finalize $(TARGET).pkg || true
	@cp -f $(TARGET).pkg $(TARGET).gnpdrm.pkg
	@cp -f $(TARGET).self build/pkg/USRDIR/EBOOT.BIN
	@pkg --contentid $(CONTENTID) build/pkg/ $(TARGET)-fself.pkg || true

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

test: tests/test_suite.c src/logger.c src/cd_drive.c src/discid.c src/metadata.c src/visualizer.c src/cJSON.c
	$(CC) $(HOST_CFLAGS) tests/test_suite.c src/logger.c src/cd_drive.c src/discid.c src/metadata.c src/visualizer.c src/cJSON.c $(HOST_LDFLAGS) -o run_tests
	@./run_tests

clean:
	rm -rf *.o src/*.o build $(TARGET).elf $(TARGET).self $(TARGET).pkg $(TARGET).gnpdrm.pkg ps3-cd-player-host run_tests cache
