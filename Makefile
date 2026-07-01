#---------------------------------------------------------------------------------
# PortNX — Nintendo Switch port browser and installer (Atmosphere, libnx)
# Single-app Makefile modeled on the proven borealis build. No brands/forwarder.
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# App metadata (also fed to nacptool by switch_rules)
#---------------------------------------------------------------------------------
APP_TITLE   := PortNX
APP_AUTHOR  := CostelaBR
APP_VERSION := 1.0.0
TARGET      := PortNX
APP_ICON    := $(CURDIR)/icon.jpg

#---------------------------------------------------------------------------------
# Layout
#---------------------------------------------------------------------------------
BUILD       := build
SOURCES     := source source/net source/catalog source/app source/ui source/download source/install
INCLUDES    := include
ROMFS       := romfs

BOREALIS_PATH      := lib/borealis
BOREALIS_RESOURCES := romfs:/

#---------------------------------------------------------------------------------
# Code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)

CFLAGS += $(INCLUDE) -D__SWITCH__ \
          -DBOREALIS_RESOURCES="\"$(BOREALIS_RESOURCES)\"" \
          -DAPP_VERSION="\"$(APP_VERSION)\"" \
          -DAPP_TITLE="\"$(APP_TITLE)\""

CXXFLAGS := $(CFLAGS) -std=gnu++20 -fexceptions -Wno-reorder

ASFLAGS := -g $(ARCH)

LDFLAGS = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# Core libraries. curl/mbedtls/zstd are linked from M0 to avoid Makefile churn
# even though networking (M1) and install (M4) consume them later.
LIBS := -lm -lcurl -lz -lmbedtls -lmbedx509 -lmbedcrypto -lzstd -lnx -lstdc++fs

LIBDIRS := $(PORTLIBS) $(LIBNX)

# borealis.mk appends its SOURCES/INCLUDES and prepends GL libs to LIBS.
include $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk

#---------------------------------------------------------------------------------
# build progress display
#---------------------------------------------------------------------------------
ifndef IPLECHO
N    := x
C     = $(words $N)$(eval N := x $N)
IPLECHO = echo -ne "\r`expr "  [\`expr $C '*' 100 / $(T)\`" : '.*\(....\)$$'`%]\033[K"
endif

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 		:=	$(OFILES_SRC)
export T	:=	$(words $(OFILES))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp --icon=$(APP_ICON)

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) $(ROMFS) clean all

all: $(BUILD)

#---------------------------------------------------------------------------------
# ROMFS: populate with borealis runtime resources (fonts, i18n, icons)
#---------------------------------------------------------------------------------
$(ROMFS):
	@[ -d $@ ] || mkdir -p $@
	@echo Merging borealis resources into ROMFS...
	@cp -ruf $(CURDIR)/$(BOREALIS_PATH)/resources/. $(CURDIR)/$(ROMFS)/
	@cp -f $(CURDIR)/icon.jpg $(CURDIR)/$(ROMFS)/icon/borealis.jpg

$(BUILD): $(ROMFS)
	@[ -d $@ ] || mkdir -p $@
	@MSYS2_ARG_CONV_EXCL="-D;$(MSYS2_ARG_CONV_EXCL)" \
	$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile TOPDIR=$(CURDIR)

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf $(ROMFS)

release: $(TARGET).nro
	@echo Creating release archive ...
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

#---------------------------------------------------------------------------------
# Silence warnings in vendored borealis sources (no -Werror, kept tidy).
#---------------------------------------------------------------------------------
THIRDPARTY_CPPOBJS := $(notdir $(wildcard $(TOPDIR)/lib/borealis/library/lib/*.cpp))

$(THIRDPARTY_CPPOBJS:.cpp=.o): CXXFLAGS := $(filter-out -Wall,$(CXXFLAGS)) -w

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
