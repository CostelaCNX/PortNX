#---------------------------------------------------------------------------------
# PortNX — Nintendo Switch port browser and installer
# Plutonium UI framework (SDL2)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# App metadata
#---------------------------------------------------------------------------------
APP_TITLE   := PortNX
APP_AUTHOR  := CostelaBR
APP_VERSION := 2.0.0
TARGET      := PortNX
APP_ICON    := $(CURDIR)/icon.jpg

#---------------------------------------------------------------------------------
# Layout
#---------------------------------------------------------------------------------
BUILD         := build
SOURCES       := source source/net source/catalog source/app source/ui source/download source/install
INCLUDES      := include
ROMFS_SRC     := romfs
ROMFS         := build/romfs
PLUTONIUM_DIR := lib/plutonium/Plutonium

#---------------------------------------------------------------------------------
# Code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)

CFLAGS += $(INCLUDE) \
          -I$(PORTLIBS)/include/freetype2 \
          -D__SWITCH__ \
          -DAPP_VERSION="\"$(APP_VERSION)\"" \
          -DAPP_TITLE="\"$(APP_TITLE)\""

CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-rtti -fexceptions -Wno-reorder

ASFLAGS := -g $(ARCH)

LDFLAGS = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lpu \
        -lSDL2_ttf -lSDL2_gfx -lSDL2_image -lSDL2_mixer \
        -lSDL2 -lEGL -lGLESv2 -lglapi -ldrm_nouveau \
        -lfreetype -lharfbuzz -lpng -ljpeg -lwebp -lbz2 \
        -lmodplug -lmpg123 -lvorbisidec -logg \
        -lm -lcurl -lz -lmbedtls -lmbedx509 -lmbedcrypto -lzstd -lnx -lstdc++fs

LIBDIRS := $(CURDIR)/$(PLUTONIUM_DIR) $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# Build progress display
#---------------------------------------------------------------------------------
ifndef IPLECHO
N    := x
C     = $(words $N)$(eval N := x $N)
IPLECHO = echo -ne "\r`expr "  [\`expr $C '*' 100 / $(T)\`" : '.*\(....\)$$'`%]\033[K"
endif

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_SRC)
export T          := $(words $(OFILES))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp --icon=$(APP_ICON) \
                   --romfsdir=$(CURDIR)/$(ROMFS)

.PHONY: $(BUILD) $(ROMFS) plutonium clean all release

all: $(BUILD)

#---------------------------------------------------------------------------------
# Plutonium: build static lib if not already built
#---------------------------------------------------------------------------------
plutonium:
	@[ -f $(CURDIR)/$(PLUTONIUM_DIR)/lib/libpu.a ] || \
	  $(MAKE) --no-print-directory -C $(CURDIR)/$(PLUTONIUM_DIR)

#---------------------------------------------------------------------------------
# ROMFS: merge project assets into build/romfs/
#---------------------------------------------------------------------------------
$(ROMFS):
	@[ -d $@ ] || mkdir -p $@
	@echo Building ROMFS...
	@cp -ruf $(CURDIR)/$(ROMFS_SRC)/. $(CURDIR)/$(ROMFS)/
	@rm -f $(CURDIR)/$(TARGET).nro

$(BUILD): $(ROMFS) plutonium
	@[ -d $@ ] || mkdir -p $@
	@MSYS2_ARG_CONV_EXCL="-D;$(MSYS2_ARG_CONV_EXCL)" \
	$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile TOPDIR=$(CURDIR)

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

release: $(TARGET).nro
	@echo Creating release archive...
	@rm -rf release_tmp
	@mkdir -p release_tmp/switch/PortNX
	@cp $(TARGET).nro release_tmp/switch/PortNX/$(TARGET).nro
	@cd release_tmp && zip -r ../$(TARGET)-v$(APP_VERSION).zip switch/
	@rm -rf release_tmp
	@echo Done: $(TARGET)-v$(APP_VERSION).zip

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf: $(OFILES)
	@echo -ne "\r[100%] Linking $(TARGET).elf\033[K\n"
	@$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

%.o: %.cpp
	@$(IPLECHO) $<
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) -c $< -o $@

%.o: %.c
	@$(IPLECHO) $<
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

%.o: %.s
	@$(IPLECHO) $<
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
