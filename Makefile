.SUFFIXES:

ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment export PSL1GHT=<path>")
endif

include $(PSL1GHT)/base_rules

TARGET      := CDLDAC001
TITLE_ID    := CDLDAC001
CONTENT_ID  := UP0001-$(TITLE_ID)_00-0000000000000000
APP_TITLE   := "PS3 CD-DA Streamer"

BUILD       := build
SOURCES     := source
OUTPUT      := $(TARGET).elf

CFLAGS      := -O2 -Wall -mcpu=cell -std=gnu99
CXXFLAGS    := $(CFLAGS)
INCLUDES    := -Iinclude -I$(PSL1GHT)/target/include

LIBDIRS     := -L$(PSL1GHT)/target/lib
LIBS        := -ldebugfont -lio -lnet -lsysnet -lsysutil -llv2

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
OBJS        := $(addprefix $(BUILD)/, $(CFILES:.c=.o))

.PHONY: all clean pkg

all: $(TARGET).self

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OUTPUT): $(OBJS)
	@echo "LD $@"
	@$(CC) $(OBJS) $(LIBDIRS) $(LIBS) -o $@

$(TARGET).self: $(OUTPUT)
	@echo "MAKE_FSELF $@"
	@make_fself $< $@

pkg: $(TARGET).self
	@echo "Generating PARAM.SFO..."
	@mkdir -p pkg/USRDIR
	@sfo.py --title $(APP_TITLE) --appid $(TITLE_ID) -f $(PSL1GHT)/target/share/psl1ght/param.sfo.xml pkg/PARAM.SFO || \
	 make_sfo -T $(APP_TITLE) -a $(TITLE_ID) pkg/PARAM.SFO
	@cp $(TARGET).self pkg/USRDIR/EBOOT.BIN
	@echo "IP=192.168.1.50:12345" > pkg/USRDIR/config.txt
	@echo "Packaging $(TARGET).pkg..."
	@pkg.py --contentid $(CONTENT_ID) pkg/ $(TARGET).pkg
	@echo "Package successfully built: $(TARGET).pkg"

clean:
	@rm -rf $(BUILD) $(OUTPUT) $(TARGET).self $(TARGET).pkg pkg
