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

include $(PSL1GHT)/ppu_rules

TARGET      := CDLDAC001
BUILD       := build
SOURCES     := source
INCLUDES    := source

CFLAGS      = -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS    = $(CFLAGS)
LDFLAGS     = $(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS        := -lio -lnet -lsysutil -lsysmodule -llv2

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
export BUILDDIR := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD   := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) $(LIBPSL1GHT_INC)
export LIBPATHS := $(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean pkg

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) *.elf *.self *.pkg

pkg: $(BUILD) $(OUTPUT).pkg

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
