# Froemix firmware - libDaisy project makefile
#
# Dependencies are pinned and patched by `make deps` (see scripts/fetch_deps.sh
# and doc/FIRMWARE.md): latest libDaisy release + DaisySP, plus ONE local
# libDaisy patch that adds 32-bit-slot TDM to SaiHandle (upstream has no TDM,
# electro-smith/libDaisy#499). Nothing else in libDaisy is modified.

TARGET = froemix

# The Seed's factory bootloader lives in internal flash and starts the app from
# QSPI, so USB DFU (`make program-dfu`) needs BOOT_QSPI. While bringing a board
# up on the bench, `make APP_TYPE=BOOT_SRAM program` runs the app from RAM
# without touching flash.
APP_TYPE ?= BOOT_QSPI

CXXFLAGS = -Wall -Wextra -Wno-unused-parameter -g -Os

CPP_SOURCES = \
	src/main.cpp \
	src/board.cpp \
	src/audio/audio6.cpp \
	src/hw/ak4619.cpp \
	src/hw/pmod.cpp \
	src/hw/controls.cpp \
	src/hw/cv.cpp \
	src/hw/sync.cpp \
	src/hw/midi_io.cpp \
	src/hw/display.cpp \
	src/engine/engine.cpp \
	src/selftest.cpp

LIBDAISY_DIR = external/libDaisy
DAISYSP_DIR  = external/DaisySP

# Our own headers live at src/; the libDaisy core Makefile appends its own.
C_INCLUDES += -Isrc

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

.PHONY: deps libs clean-all size check-pins

deps:
	./scripts/fetch_deps.sh

$(LIBDAISY_DIR)/build/libdaisy.a:
	$(MAKE) -C $(LIBDAISY_DIR)

$(DAISYSP_DIR)/build/libdaisysp.a:
	$(MAKE) -C $(DAISYSP_DIR)

libs: $(LIBDAISY_DIR)/build/libdaisy.a $(DAISYSP_DIR)/build/libdaisysp.a

$(BUILD_DIR)/$(TARGET).elf: libs

# The firmware pin map is checked against the board source, not trusted.
check-pins:
	python3 ../tools/check_firmware_pins.py

# Report the linked image size (flash/SDRAM budget is real on this board).
size: $(BUILD_DIR)/$(TARGET).elf
	$(SZ) $(BUILD_DIR)/$(TARGET).elf

# Remove build output AND the fetched dependencies.
clean-all: clean
	rm -rf external build
