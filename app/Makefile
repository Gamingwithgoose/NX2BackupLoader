# Native Nintendo Switch 2 / kaiHEN build using ouncebrew libounce.
.SUFFIXES:
.DEFAULT_GOAL := all
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif
ifeq ($(wildcard $(DEVKITPRO)/libounce/switch2_rules),)
$(error Missing ounce-devkit libounce/switch2_rules. Switch 1 libnx builds are forbidden)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libounce/switch2_rules

TARGET := nx2backuploader
BUILD := build
SOURCES := source
INCLUDES :=
ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX)
export NO_ICON := 1
export NO_NACP := 1

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export OFILES := $(addsuffix .o,$(basename $(notdir $(wildcard $(SOURCES)/*.c))))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).map
else
DEPENDS := $(OFILES:.o=.d)
-include $(DEPENDS)
.PHONY: all
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf
$(OFILES): $(TOPDIR)/Makefile
$(OUTPUT).elf: $(OFILES) $(TOPDIR)/Makefile
	$(LINK.o) $(OFILES) $(LIBPATHS) $(LIBS) -o $@
endif
