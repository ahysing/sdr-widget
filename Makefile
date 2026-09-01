# Top-level build for AVR32 firmware (Release/widget.elf), host tools, and PC unit tests.
#
# Firmware profiles (pick one target):
#   make audio-widget         Default: DA256 + HW_GEN_SPRX (upstream audio-widget-experimental).
#   make audio-widget-mk3     Henry Audio USB DAC 128 Mk3: FEATURE_PRODUCT_AB1x + HW_GEN_AB1X.
#   scripts/build-ha128.sh    Same as audio-widget-mk3 (clean Release + relink).
#   make audio-widget-84mhz   audio-widget + FEATURE_84MHz (experimental clock).
#
# After changing AUDIO_WIDGET_DEFAULTS or product flags, rebuild features.o (targets rm it) or use mk3 clean.
#
# Variables (override on command line):
#   PARTNAME                  AVR32 -mpart=... (default uc3a3128).
#   AUDIO_WIDGET_FEATURE_FLAGS  Board/image/feature defaults + product macros (see features.h).
#   AUDIO_WIDGET_DEFAULTS     PARTNAME + feature flags + BUILD_INFO_DEFS; full CFLAGS prefix for firmware.
#   AUDIO_WIDGET_MK3_DEFAULTS Mk3 profile: undef DA256/SPRX/HID, add AB1x + volume + ALT2 + cfg + VDD_SENSE.
#   CONFIGURATION             Release (adds -DRELEASE) or Debug.
#   LOUDNESS_DISABLE=1        -DLOUDNESS_DISABLE
#   USBSTATISTICS_DISABLE=1   -DUSBSTATISTICS_DISABLE
#   LOUDNESS_DB_SPL_MAX       Peak SPL at 0 dBFS (default 85; must exceed firmware ref 80).
#   WIDGET_MAKE_ENV           Exported CFLAGS/ASFLAGS/optimization bundle passed to ./make-widget.
#   WIDGET_CONTROL_CFLAGS     Host builds only (no -mpart); should match flashed feature flags.
#
# BUILD_* tokens are bare numbers/strings for the C preprocessor (see device_mouse_hid_task.c).

include makefile.msys2

PARTNAME = -mpart=uc3a3128

BUILD_COMMIT := $(shell git rev-parse --short HEAD)
ifeq ($(strip $(BUILD_COMMIT)),)
  BUILD_COMMIT := unknown
endif
BUILD_DIRTY := $(shell git diff --quiet && git diff --cached --quiet && echo clean || echo dirty)

ifneq ($(OS),Windows_NT)
BUILD_DATE := $(shell date '+%Y%m%d')
BUILD_TIME := $(shell date '+%H%M')
BUILD_TZ := $(shell date '+%Z')
else ifdef IS_MSYS
BUILD_DATE := $(shell date '+%Y%m%d')
BUILD_TIME := $(shell date '+%H%M')
BUILD_TZ := UTC
else
BUILD_DATE := $(shell powershell -NoProfile -Command "Get-Date -Format yyyyMMdd")
BUILD_TIME := $(shell powershell -NoProfile -Command "Get-Date -Format HHmm")
BUILD_TZ := UTC
endif

BUILD_INFO_DEFS = -DBUILD_DATE=$(BUILD_DATE) -DBUILD_TIME=$(BUILD_TIME) -DBUILD_TZ=$(BUILD_TZ) -DBUILD_COMMIT=$(BUILD_COMMIT) -DBUILD_DIRTY=$(BUILD_DIRTY)

AUDIO_WIDGET_FEATURE_FLAGS = \
	-DFEATURE_BOARD_DEFAULT=feature_board_usbi2s \
	-DFEATURE_IMAGE_DEFAULT=feature_image_uac2_audio \
	-DFEATURE_IN_DEFAULT=feature_in_normal \
	-DFEATURE_OUT_DEFAULT=feature_out_normal \
	-DFEATURE_ADC_DEFAULT=feature_adc_none \
	-DFEATURE_DAC_DEFAULT=feature_dac_generic \
	-DFEATURE_LCD_DEFAULT=feature_lcd_none \
	-DFEATURE_LOG_DEFAULT=feature_log_none \
	-DFEATURE_FILTER_DEFAULT=feature_filter_fir \
	-DFEATURE_QUIRK_DEFAULT=feature_quirk_none \
	-DUSB_STATE_MACHINE_GPIO \
	-DFEATURE_HID \
	-DFEATURE_PRODUCT_DA256 \
	-DFEATURE_ALT2_16BIT \
	-DFEATURE_SPDIF_CMD \
	-DHW_GEN_SPRX

AUDIO_WIDGET_DEFAULTS ?= $(PARTNAME) \
	$(AUDIO_WIDGET_FEATURE_FLAGS) \
	$(BUILD_INFO_DEFS)

HA128_MK3_PARTNAME = -mpart=uc3a3128
HA128_MK3_UNDEF_FLAGS = -UFEATURE_PRODUCT_DA256 -UFEATURE_SPDIF_CMD -UHW_GEN_SPRX -UFEATURE_HID
HA128_MK3_DEF_FLAGS = -DFEATURE_PRODUCT_AB1x -DHW_GEN_AB1X -DFEATURE_VOLUME_CTRL \
	-DFEATURE_ALT2_16BIT -DFEATURE_CFG_INTERFACE -DVDD_SENSE
AUDIO_WIDGET_MK3_DEFAULTS = $(HA128_MK3_PARTNAME) $(AUDIO_WIDGET_FEATURE_FLAGS) \
	$(BUILD_INFO_DEFS) $(HA128_MK3_UNDEF_FLAGS) $(HA128_MK3_DEF_FLAGS)

LOUDNESS_DISABLE ?= 0
USBSTATISTICS_DISABLE ?= 0
CONFIGURATION ?= Release
LOUDNESS_DB_SPL_MAX ?= 85
CFLAGS_LOUDNESS_DB_SPL_MAX = -DLOUDNESS_DB_SPL_MAX=$(LOUDNESS_DB_SPL_MAX)

CFLAGS_LOUDNESS =
ifeq ($(LOUDNESS_DISABLE),1)
  CFLAGS_LOUDNESS_DISABLE = -DLOUDNESS_DISABLE
else
  CFLAGS_LOUDNESS_DISABLE =
endif
ifeq ($(USBSTATISTICS_DISABLE),1)
  CFLAGS_LOUDNESS_USB_STATS_EVENTS = -DUSBSTATISTICS_DISABLE
else
  CFLAGS_LOUDNESS_USB_STATS_EVENTS =
endif

ifeq ($(CONFIGURATION),Release)
  CFLAGS_CONFIGURATION = -DRELEASE
  CONFIGURATION_MSG = Release configuration: configDBG is 0. Deactivating debug trace.
else
  CFLAGS_CONFIGURATION =
  CONFIGURATION_MSG = Debug configuration: configDBG is 1. Activating debug trace.
endif

ifeq ($(CONFIGURATION),Release)
  CFLAGS_OPT_FP = -fomit-frame-pointer
else
  CFLAGS_OPT_FP =
endif

CFLAGS_OPTIMIZATIONS = -O3 -fno-strict-aliasing -funroll-loops $(CFLAGS_OPT_FP) \
  -ffunction-sections -fdata-sections -mno-pic -mimm-in-const-pool
CFLAGS_FREERTOS_OPTIMIZATIONS = -O3
LDFLAGS_APP_OPTIMIZATIONS = -Wl,--gc-sections
CFLAGS_LOUDNESS_HOT = -O3 -funroll-loops -finline-functions \
  -fno-strict-aliasing

WIDGET_LOUDNESS_FLAGS = $(CFLAGS_LOUDNESS) $(CFLAGS_LOUDNESS_DISABLE) $(CFLAGS_LOUDNESS_USB_STATS_EVENTS) $(CFLAGS_LOUDNESS_DB_SPL_MAX)
AUDIO_WIDGET_CFLAGS = $(AUDIO_WIDGET_DEFAULTS) $(WIDGET_LOUDNESS_FLAGS) $(CFLAGS_CONFIGURATION)

WIDGET_DEFAULTS ?= $(AUDIO_WIDGET_DEFAULTS)
WIDGET_CONTROL_CFLAGS = $(AUDIO_WIDGET_FEATURE_FLAGS) $(BUILD_INFO_DEFS) $(WIDGET_LOUDNESS_FLAGS)

WIDGET_MAKE_ENV = CFLAGS="$(AUDIO_WIDGET_CFLAGS)" \
	ASFLAGS="$(PARTNAME)" \
	CFLAGS_APP_OPTIMIZATIONS="$(CFLAGS_OPTIMIZATIONS)" \
	CFLAGS_FREERTOS_OPTIMIZATIONS="$(CFLAGS_FREERTOS_OPTIMIZATIONS)" \
	LDFLAGS_APP_OPTIMIZATIONS="$(LDFLAGS_APP_OPTIMIZATIONS)" \
	CFLAGS_LOUDNESS_HOT="$(CFLAGS_LOUDNESS_HOT)"

ifeq ($(OS),Windows_NT)
  EXE_EXT = .exe
  VCPKG_DIR ?= C:/Users/AHysing/code/vcpkg
  VCPKG_INSTALLED ?= $(VCPKG_DIR)/installed/x64-windows
  ifdef IS_MSYS
    CC ?= gcc
  else
    ifeq ($(origin CC),default)
      CC = cl
    endif
    CC ?= cl
  endif
  USE_MSVC = $(findstring cl,$(CC))
else
  EXE_EXT =
  CC ?= gcc
  USE_MSVC =
endif

ifneq ($(USE_MSVC),)
  LIBUSB_INCLUDE ?= $(VCPKG_INSTALLED)/include
  LIBUSB_LIBDIR ?= $(VCPKG_INSTALLED)/lib
else ifdef IS_MSYS
  LIBUSB_INCLUDE ?= $(MSYS_TOOLCHAIN_ROOT)/include
  LIBUSB_LIBDIR ?= $(MSYS_TOOLCHAIN_ROOT)/lib
else
  LIBUSB_INCLUDE ?= /usr/include
  LIBUSB_LIBDIR ?= /usr/lib
endif
LIBUSB_HEADER = $(LIBUSB_INCLUDE)/libusb-1.0/libusb.h

CFLAGS_TEST = -DBUILD_TESTING
TEST_BUILD_DIR = Release/tests/pc
VCVARS64 ?= C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat

ifneq ($(USE_MSVC),)
  TEST_PREAMBLE = /FItests/pc/compiler.h
else
  TEST_PREAMBLE = -include tests/pc/compiler.h
endif

TEST_CFLAGS = $(CFLAGS_LOUDNESS) $(CFLAGS_LOUDNESS_DISABLE) $(CFLAGS_LOUDNESS_USB_STATS_EVENTS) $(CFLAGS_LOUDNESS_DB_SPL_MAX)

ifeq ($(OS),Windows_NT)
ifndef IS_MSYS
  TEST_MKDIR = if not exist $(subst /,\,$(TEST_BUILD_DIR)) mkdir $(subst /,\,$(TEST_BUILD_DIR))
  TEST_RMDIR = if exist $(subst /,\,$(TEST_BUILD_DIR)) rmdir /s /q $(subst /,\,$(TEST_BUILD_DIR))
else
  TEST_MKDIR = mkdir -p $(TEST_BUILD_DIR)
  TEST_RMDIR = rm -rf $(TEST_BUILD_DIR)
endif
else
  TEST_MKDIR = mkdir -p $(TEST_BUILD_DIR)
  TEST_RMDIR = rm -rf $(TEST_BUILD_DIR)
endif

ifneq ($(USE_MSVC),)
  OUT_FLAG = /Fe:
  OBJ_DIR_FLAG = /Fo$(TEST_BUILD_DIR)/
  LINK_USB = /link /LIBPATH:"$(LIBUSB_LIBDIR)" libusb-1.0.lib
  CFLAGS_COMMON = /nologo /I. /Isrc /Isrc/CONFIG /I"$(LIBUSB_INCLUDE)" $(TEST_CFLAGS)
else
  OUT_FLAG = -o
  OBJ_DIR_FLAG =
  LINK_USB = -L$(LIBUSB_LIBDIR) -lusb-1.0
  CFLAGS_COMMON = -I. -Isrc -I$(LIBUSB_INCLUDE) $(TEST_CFLAGS)
endif

CFLAGS_TEST_BUILD = -I tests/pc $(CFLAGS_COMMON)

ifeq ($(OS),Windows_NT)
ifdef IS_MSYS
  RM = rm -f
  FIX_PATH = $(1)
else
  RM = cmd /c del /f /q
  FIX_PATH = $(subst /,\,$(1))
endif
else
  RM = rm -f
  FIX_PATH = $(1)
endif

ifdef IS_MSYS
  WIN_CURDIR = $(shell cygpath -m '$(CURDIR)')
endif

.PHONY: all test run-test clean clean-test help check-libusb \
	audio-widget audio-widget-mk3 ha128 audio-widget-84mhz sdr-widget build-audio-widget build-sdr-widget test-avr32 \
	widget-control henryctl print-audio-widget-defaults print-audio-widget-mk3-defaults

print-audio-widget-defaults:
	@echo $(AUDIO_WIDGET_DEFAULTS)

print-audio-widget-mk3-defaults:
	@echo $(AUDIO_WIDGET_MK3_DEFAULTS)

all:: Release/widget.elf widget-control$(EXE_EXT) henryctl$(EXE_EXT)

Release/widget.elf::
	@echo $(CONFIGURATION_MSG)
	rm -f Release/widget.elf Release/src/features.o
	$(WIDGET_MAKE_ENV) ./make-widget

audio-widget::
	@echo "WARNING: DA256/SPRX profile ΓÇö not for Henry Audio USB DAC 128 Mk3; use: make audio-widget-mk3"
	@echo $(CONFIGURATION_MSG)
	rm -f Release/widget.elf Release/src/features.o
	$(WIDGET_MAKE_ENV) ./make-widget

audio-widget-mk3 ha128::
	@echo "Mk3 profile: FEATURE_PRODUCT_AB1x + HW_GEN_AB1X ($(HA128_MK3_PARTNAME))"
	$(MAKE) -C Release clean
	$(MAKE) audio-widget \
		PARTNAME="$(HA128_MK3_PARTNAME)" \
		AUDIO_WIDGET_DEFAULTS="$(AUDIO_WIDGET_MK3_DEFAULTS)"
	$(MAKE) -C Release all ASFLAGS="$(HA128_MK3_PARTNAME)"

audio-widget-84mhz::
	@echo $(CONFIGURATION_MSG)
	rm -f Release/widget.elf Release/src/features.o
	CFLAGS="$(AUDIO_WIDGET_CFLAGS) -DFEATURE_84MHz" ASFLAGS="$(PARTNAME)" \
		CFLAGS_APP_OPTIMIZATIONS="$(CFLAGS_OPTIMIZATIONS)" \
		CFLAGS_FREERTOS_OPTIMIZATIONS="$(CFLAGS_FREERTOS_OPTIMIZATIONS)" \
		LDFLAGS_APP_OPTIMIZATIONS="$(LDFLAGS_APP_OPTIMIZATIONS)" \
		CFLAGS_LOUDNESS_HOT="$(CFLAGS_LOUDNESS_HOT)" \
		./make-widget

widget-control: widget-control$(EXE_EXT)

henryctl:
	+$(MAKE) -C henryctl henryctl$(EXE_EXT)

check-libusb:
ifeq ($(wildcard $(LIBUSB_HEADER)),)
	@echo "libusb headers not found at $(LIBUSB_HEADER)"
ifdef IS_MSYS
	@echo "MSYS2: install libusb, then rebuild:"
	@echo "  pacman -S --needed mingw-w64-ucrt-x86_64-libusb make"
	@echo "  make widget-control"
else ifdef USE_MSVC
	@echo "MSVC: install libusb via vcpkg, then rebuild:"
	@echo "  vcpkg install libusb:x64-windows"
	@echo "  make widget-control VCPKG_DIR=<path-to-vcpkg>"
else
	@echo "Install libusb development package for your platform."
endif
	@exit 1
endif

widget-control$(EXE_EXT): widget-control.c src/features.h | check-libusb
	$(CC) $(WIDGET_CONTROL_CFLAGS) $(CFLAGS_COMMON) $(OUT_FLAG)$@ widget-control.c $(LINK_USB)

henryctl$(EXE_EXT):
	+$(MAKE) -C henryctl henryctl$(EXE_EXT)

clean:: clean-test
	rm -f widget-control widget-control.exe henryctl.exe
ifeq ($(OS),Windows_NT)
ifndef IS_MSYS
	-cmd /c "del /f /q henryctl\henryctl henryctl\henryctl.exe henryctl\*.obj henryctl\*.o 2>NUL"
else
	rm -f henryctl/henryctl henryctl/henryctl.exe henryctl/*.obj henryctl/*.o
endif
else
	rm -f henryctl/henryctl henryctl/henryctl.exe henryctl/*.obj henryctl/*.o
endif
	$(MAKE) -C Release clean

clean-test:
	$(TEST_RMDIR)
ifeq ($(OS),Windows_NT)
ifndef IS_MSYS
	-if exist *.obj del /f /q *.obj
else
	rm -f *.obj
endif
else
	rm -f *.obj
endif

$(TEST_BUILD_DIR):
	$(TEST_MKDIR)

$(TEST_BUILD_DIR)/loudness_equalizer_step_switch_stats_tests$(EXE_EXT): tests/pc/loudness_equalizer_step_switch_stats_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_highres.c src/loudness_inferred_gain.c src/usb_statistics.c src/stats_telemetry.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) -DUNIT_TEST -DFEATURE_VOLUME_CTRL $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_equalizer_step_switch_stats_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_highres.c src/loudness_inferred_gain.c src/usb_statistics.c src/stats_telemetry.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c

$(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT): tests/pc/loudness_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_highres.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) -DUSBSTATISTICS_DISABLE -DFEATURE_VOLUME_CTRL $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_highres.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c

$(TEST_BUILD_DIR)/loudness_inferred_gain_tests$(EXE_EXT): tests/pc/loudness_inferred_gain_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_highres.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) -DUSBSTATISTICS_DISABLE -DFEATURE_VOLUME_CTRL $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_inferred_gain_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_highres.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c

$(TEST_BUILD_DIR)/usb_statistics_tests$(EXE_EXT): tests/pc/usb_statistics_tests.c src/usb_statistics.c src/stats_telemetry.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) -DUNIT_TEST $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/usb_statistics_tests.c src/usb_statistics.c src/stats_telemetry.c

$(TEST_BUILD_DIR)/audio_stats_logic_tests$(EXE_EXT): tests/pc/audio_stats_logic_tests.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/audio_stats_logic_tests.c

RUN_TEST_EXES = $(TEST_BUILD_DIR)/audio_stats_logic_tests$(EXE_EXT) \
	$(TEST_BUILD_DIR)/usb_statistics_tests$(EXE_EXT)
ifneq ($(LOUDNESS_DISABLE),1)
RUN_TEST_EXES += $(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT)

RUN_TEST_EXES += $(TEST_BUILD_DIR)/loudness_inferred_gain_tests$(EXE_EXT)
ifneq ($(USBSTATISTICS_DISABLE),1)
RUN_TEST_EXES += $(TEST_BUILD_DIR)/loudness_equalizer_step_switch_stats_tests$(EXE_EXT)
endif
endif

test:
ifeq ($(OS),Windows_NT)
ifdef IS_MSYS
	@$(MAKE) run-test LOUDNESS_DISABLE=$(LOUDNESS_DISABLE) USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)
else
ifeq ($(wildcard run-pc-tests.cmd),)
	@$(MAKE) run-test LOUDNESS_DISABLE=$(LOUDNESS_DISABLE) USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)
else
	@cmd /c run-pc-tests.cmd $(LOUDNESS_DISABLE) $(USBSTATISTICS_DISABLE)
endif
endif
else
	@$(MAKE) run-test LOUDNESS_DISABLE=$(LOUDNESS_DISABLE) USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)
endif

run-test: $(RUN_TEST_EXES)
	$(TEST_BUILD_DIR)/audio_stats_logic_tests$(EXE_EXT)
ifneq ($(LOUDNESS_DISABLE),1)
ifneq ($(USBSTATISTICS_DISABLE),1)
	$(TEST_BUILD_DIR)/loudness_equalizer_step_switch_stats_tests$(EXE_EXT)
endif
	$(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT)

	$(TEST_BUILD_DIR)/loudness_inferred_gain_tests$(EXE_EXT)
endif
	$(TEST_BUILD_DIR)/usb_statistics_tests$(EXE_EXT)

test-avr32: tests/avr32/statistics_avr32_tests.o tests/avr32/loudness_fast_avr32.o

tests/avr32/statistics_avr32_tests.o: tests/avr32/statistics_avr32_tests.c
	$(if $(AVR32BIN),"$(AVR32BIN)/avr32-gcc",avr32-gcc) -DBOARD=SDRwdgtLite -DFREERTOS_USED -Isrc/SOFTWARE_FRAMEWORK/UTILS/DEBUG -Isrc/SOFTWARE_FRAMEWORK/SERVICES/USB -Isrc/CONFIG -Isrc/SOFTWARE_FRAMEWORK/UTILS/PREPROCESSOR -Isrc/SOFTWARE_FRAMEWORK/UTILS -Isrc/SOFTWARE_FRAMEWORK/BOARDS -Isrc -mpart=uc3a3256 -c -o $@ $<

tests/avr32/loudness_fast_avr32.o: src/loudness_fast.c
	$(if $(AVR32BIN),"$(AVR32BIN)/avr32-gcc",avr32-gcc) -std=gnu99 -DBOARD=SDRwdgtLite -DFREERTOS_USED -Isrc/SOFTWARE_FRAMEWORK/SERVICES/FREERTOS/Source/include -Isrc/SOFTWARE_FRAMEWORK/SERVICES/FREERTOS/Source/portable/GCC/AVR32_UC3 -Isrc/SOFTWARE_FRAMEWORK/DRIVERS/INTC -Isrc/SOFTWARE_FRAMEWORK/UTILS/DEBUG -Isrc/SOFTWARE_FRAMEWORK/SERVICES/USB -Isrc/CONFIG -Isrc/SOFTWARE_FRAMEWORK/UTILS/PREPROCESSOR -Isrc/SOFTWARE_FRAMEWORK/UTILS -Isrc/SOFTWARE_FRAMEWORK/BOARDS -Isrc -mpart=uc3a3256 -c -o $@ $<

help:
	@echo "Firmware targets:"
	@echo "  make audio-widget              DA256 / SPRX only (NOT Mk3 ΓÇö wrong USB identity on HA128)"
	@echo "  make audio-widget-mk3          Henry Audio USB DAC 128 Mk3 (FEATURE_PRODUCT_AB1x)"
	@echo "  make audio-widget-84mhz        Build Release/widget.elf (DA256 / SPRX + FEATURE_84MHz)"
	@echo "  scripts/build-ha128.sh         Same as make audio-widget-mk3 (clean + Mk3 flags)"
	@echo "  make all                       widget.elf + widget-control.exe + henryctl.exe"
	@echo "  make clean                     Remove firmware and test build artifacts"
	@echo ""
	@echo "Loudness / statistics options (all features enabled by default):"
	@echo "  LOUDNESS_DB_SPL_MAX=N        Peak dB SPL at 0 dBFS gain (default: 85, must be > 80)"
	@echo "  LOUDNESS_DISABLE=1           Omit loudness filter from firmware"
	@echo "  USBSTATISTICS_DISABLE=1"
	@echo "                               Omit loudness equalizer-step USB events"
	@echo "  CONFIGURATION=Release|Debug  App src/*.c optimization level (default: Release)"
	@echo ""
	@echo "Examples:"
	@echo "  make audio-widget LOUDNESS_DB_SPL_MAX=106"
	@echo "  make audio-widget LOUDNESS_DISABLE=1"
	@echo "  make audio-widget USBSTATISTICS_DISABLE=1"
	@echo "  make test LOUDNESS_DISABLE=1"
	@echo "  make test USBSTATISTICS_DISABLE=1"
	@echo ""
	@echo "Host tools (widget-control & henryctl):"
	@echo "  make widget-control            Build widget-control$(EXE_EXT) (SDR-Widget features API)"
	@echo "  make henryctl                  Build henryctl$(EXE_EXT) in henryctl/ (bass boost / loudness gate)"
	@echo "  MSYS2: use the UCRT64 terminal (not plain MSYS) ΓÇö pacman -S mingw-w64-ucrt-x86_64-{gcc,libusb} make"
	@echo "  PowerShell:   vcpkg install libusb:x64-windows-static, run .\vcvars64.ps1, then make henryctl (or .\henryctl\build.ps1)"
	@echo "  Override:     make henryctl VCPKG_DIR=C:/path/to/vcpkg"
	@echo ""
	@echo "PC unit tests (MSVC on Windows):"
	@echo "  make test                      Build and run applicable test suites"
	@echo "  make clean-test                Remove Release/tests/pc"
	@echo ""
	@echo "Current settings:"
	@echo "  LOUDNESS_DISABLE=$(LOUDNESS_DISABLE)"
	@echo "  USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)"
	@echo "  CONFIGURATION=$(CONFIGURATION)"
