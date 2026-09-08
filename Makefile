
# These defaults are compiled into code, not necessarily forced
# into flash. To force them into flash, reboot with 
# feature_quirk_ptest set in flash, which will lead to flash being
# overwritten with defaults
#
# -DFEATURE_PRODUCT_AB1x		Henry Audio USB DAC 128 and QNKTC series of DACs
# -DFEATURE_PRODUCT_DA256		Henry Audio experimental product
# -DFEATURE_PRODUCT_BOEC1		Boenicke experimental product
# -DFEATURE_PRODUCT_OEM01		OEM product on SPRX rev. N: SPDIF0, TOSLINK0/1, AESEBU0, 8:1 S/PDIF mux, version ADC. Build with make clean audio-widget OEM_MANUFACTURER="Manufacturer" OEM_PRODUCT="Product"
# -DFEATURE_PRODUCT_FMADC		Floormotion data collection device
#
# -DHW_GEN_AB1X					Pure USB DAC - all marketed Henry Audio / QNKTC hardware versions
# -DHW_GEN_FMADC				Floormotion ADC data collection based on old USB module
# -DHW_GEN_SPRX					Latest revision of SPDIF receiver
# -DHW_GEN_SPRX_PATCH_02		Hardware development, RXmod_T1_C boards
#
# -DFEATURE_HID					USB HID functions AND debug development system - FIX: split them in two... 
# -DFEATURE_VOLUME_CTRL			Software volume control in DAC
# -DFEATURE_CLOCK_SELECTOR		Clock selector, don't enable unless you wish to experiment with different clocks for ADC and DAC
# -DFEATURE_ALT2_16BIT			UAC2 has both ALT1 (24 bit) and ALT2 (16 bit). Applies to both ADC and DAC code in UAC2. Does NOT! apply to UAC1
#
# -DFEATURE_ADC_EXPERIMENTAL	Experimental ADC support
# -DFEATURE_SPDIF_CMD			Experimental disable state machine in SPDIF receiver code - only makes sense in SPRX context
#
# -DUSB_REDUCED_DEBUG			Reduce debug and use UART only for CPU communication
# -DFEATURE_UNINVERT_LRCK		Reverse the effect of an inverted LRCK signal on the I2S output
# -DBOOT_SIGNAL_LED				Use front LED to indicate reboot
# -DLOOSE_SIGNAL_LED			Use front LED to indicate SPDIF signal loss
# -DI2S_METADATA				Send 8 bits of metadata below each 24-bit audio data sample
# -DI2S_POLARITY_CHECK			Output SDATA in phase with LRCK for debugging. It will override -DI2S_METADATA for paused content
# Targets: audio-widget (HA256), audio-widget-mk3, audio-widget-84mhz, henryctl, test.
# Optional: LOUDNESS_DISABLE=1 USBSTATISTICS_DISABLE=1 LOUDNESS_DB_SPL_MAX=N CONFIGURATION=Release|Debug

# -DINDICATE_EXTREME_SI			Indication of extreme skip/insert events
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

BUILD_INFO_DEFS = -DBUILD_DATE=$(BUILD_DATE) -DBUILD_TIME=$(BUILD_TIME) -DBUILD_TZ=$(BUILD_TZ) -DBUILD_COMMIT=$(BUILD_COMMIT) -DBUILD_DIRTY=$(BUILD_DIRTY) \
	-DBUILD_HHMM_NUM=1$(BUILD_TIME)
# 20261101: BUILD_HHMM_NUM = 1HHMM for USB serial in usb_descriptors.h (leading 1 avoids invalid octal).

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
	$(BUILD_INFO_DEFS) \
	$(PRODUCT_NAMES_DEFS)

ifneq (,$(findstring -DFEATURE_PRODUCT_OEM01,$(AUDIO_WIDGET_DEFAULTS)))
  ifneq ($(MAKECMDGOALS),clean)
    ifneq ($(origin OEM_MANUFACTURER),command line)
      $(error FEATURE_PRODUCT_OEM01 needs OEM_MANUFACTURER and OEM_PRODUCT on the make command line, e.g. make clean audio-widget OEM_MANUFACTURER="Manufacturer" OEM_PRODUCT="Product")
    endif
    ifneq ($(origin OEM_PRODUCT),command line)
      $(error FEATURE_PRODUCT_OEM01 needs OEM_MANUFACTURER and OEM_PRODUCT on the make command line, e.g. make clean audio-widget OEM_MANUFACTURER="Manufacturer" OEM_PRODUCT="Product")
    endif
    ifeq ($(strip $(OEM_MANUFACTURER)),)
      $(error OEM_MANUFACTURER is empty)
    endif
    ifeq ($(strip $(OEM_PRODUCT)),)
      $(error OEM_PRODUCT is empty)
    endif
  endif
  PRODUCT_NAMES_DEFS = -DPRODUCT_NAMES_LOCAL
  PRODUCT_NAMES_GEN = sh ./make-names.sh "$(OEM_MANUFACTURER)" "$(OEM_PRODUCT)" > src/product_names_local.h
else
  PRODUCT_NAMES_DEFS =
  PRODUCT_NAMES_GEN = true
endif

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

.PHONY: all test run-test clean clean-test check-libusb \
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

# Floormotion after "\"
#	-DFEATURE_PRODUCT_FMADC \
#	-DFEATURE_ADC_EXPERIMENTAL \
#	-DHW_GEN_FMADC

# Henry Audio USB DAC 128 / QNKTC after "\" 
#	-DFEATURE_PRODUCT_AB1x \
#	-DFEATURE_ALT2_16BIT \
#	-DHW_GEN_AB1X \
#	-DFEATURE_VOLUME_CTRL
# 20261010: VDD_SENSE (PA19 digital read -> self/bus-powered descriptor) is no longer defined anywhere.
# AB-1.x is bus-powered only. PA19 isn't connected on early HW_GEN_SPRX boards and is the BOM_VER
# ADC input (AD[5]) on newer ones. SPRX (DA 256 and onwards) sets bus/self-powered per USB plug, see mobo_usb_select()

# Henry Audio USB DA 256 (SPRX) after "\" 
#	-DFEATURE_PRODUCT_DA256 \
#	-DFEATURE_ALT2_16BIT \
#	-DHW_GEN_SPRX

# SPRX debug after "\" 
#	-DFEATURE_PRODUCT_DA256 \
#	-DI2S_POLARITY_CHECK \
# or
#	-DI2S_METADATA \
#	-DFEATURE_ALT2_16BIT \
#	-DHW_GEN_SPRX \
#	-DFEATURE_VOLUME_CTRL \
#	-DBOOT_SIGNAL_LED \
#	-DFEATURE_SPDIF_CMD

# OEM01 (SPRX rev. N) after "\". Needs OEM_MANUFACTURER and OEM_PRODUCT, see above
#	-DFEATURE_PRODUCT_OEM01 \
#	-DFEATURE_ALT2_16BIT \
#	-DFEATURE_SPDIF_CMD \
#	-DHW_GEN_SPRX

# Boenicke (SPRX) after "\" NB: check register 0x08 / R8 of WM8804 to enable regenerated MCLK when that is needed 
#	-DFEATURE_PRODUCT_BOEC1 \
#	-DI2S_POLARITY_CHECK \
# or
#	-DI2S_METADATA \
#	-DUSB_REDUCED_DEBUG \
#	-DFEATURE_ALT2_16BIT \
#	-DHW_GEN_SPRX
	

audio-widget::
	@echo "WARNING: DA256/SPRX profile - not for Henry Audio USB DAC 128 Mk3; use: make audio-widget-mk3"
	@echo $(CONFIGURATION_MSG)
	rm -f Release/widget.elf Release/src/features.o
	$(PRODUCT_NAMES_GEN)
	$(WIDGET_MAKE_ENV) ./make-widget

audio-widget-mk3 ha128::
	@echo "Mk3 profile: FEATURE_PRODUCT_AB1x + HW_GEN_AB1X ($(HA128_MK3_PARTNAME))"
	$(MAKE) -C Release clean
	$(MAKE) audio-widget \
		PARTNAME="$(HA128_MK3_PARTNAME)" \
		AUDIO_WIDGET_DEFAULTS="$(AUDIO_WIDGET_MK3_DEFAULTS)"
	$(MAKE) -C Release all ASFLAGS="$(HA128_MK3_PARTNAME)"

# 20260915: 84MHz CPU clock bring-up for 384kHz UAC2 work. Same as audio-widget + FEATURE_84MHz.
audio-widget-84mhz::
	@echo $(CONFIGURATION_MSG)
	rm -f Release/widget.elf Release/src/features.o
	$(PRODUCT_NAMES_GEN)
	CFLAGS="$(AUDIO_WIDGET_CFLAGS) -DFEATURE_84MHz" ASFLAGS="$(PARTNAME)" \
		CFLAGS_APP_OPTIMIZATIONS="$(CFLAGS_OPTIMIZATIONS)" \
		CFLAGS_FREERTOS_OPTIMIZATIONS="$(CFLAGS_FREERTOS_OPTIMIZATIONS)" \
		LDFLAGS_APP_OPTIMIZATIONS="$(LDFLAGS_APP_OPTIMIZATIONS)" \
		CFLAGS_LOUDNESS_HOT="$(CFLAGS_LOUDNESS_HOT)" \
		./make-widget

widget-control: widget-control$(EXE_EXT)

henryctl henryctl$(EXE_EXT):
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

$(TEST_BUILD_DIR)/loudness_equalizer_step_switch_stats_tests$(EXE_EXT): tests/pc/loudness_equalizer_step_switch_stats_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_inferred_gain.c src/usb_statistics.c src/stats_telemetry.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) -DUNIT_TEST -DFEATURE_VOLUME_CTRL $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_equalizer_step_switch_stats_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_inferred_gain.c src/usb_statistics.c src/stats_telemetry.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c

$(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT): tests/pc/loudness_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) -DUSBSTATISTICS_DISABLE -DFEATURE_VOLUME_CTRL $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c

$(TEST_BUILD_DIR)/loudness_inferred_gain_tests$(EXE_EXT): tests/pc/loudness_inferred_gain_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests $(CFLAGS_TEST_BUILD) $(CFLAGS_TEST) -DUSBSTATISTICS_DISABLE -DFEATURE_VOLUME_CTRL $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_inferred_gain_tests.c src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_internal.c src/loudness_inferred_gain.c tests/pc/usb_volume_stub.c tests/pc/device_audio_volume_stub.c

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
