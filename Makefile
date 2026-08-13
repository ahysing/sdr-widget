##
## make both the widget
## and the widget-control that matches
## the features in the widget
##
## usage: "make audio-widget" or "make sdr-widget"
## IMPORTANT: run "make clean" if you change compilation defaults or 
## targets!

## Hardware variety selects USB VID/PID and signature fields only
## Available defines:
## -DCOMPILING_FOR_DRIVER_DEVELOPMENT uses "internal lab use only" VID/PID
## -DFEATURE_PRODUCT_SDR_WIDGET uses AUDIO_PRODUCT_ID_1 and _2
## -DFEATURE_PRODUCT_USB9023 uses AUDIO_PRODUCT_ID_3 and _4
## -DFEATURE_PRODUCT_USB5102 uses AUDIO_PRODUCT_ID_5 and _6
## -DFEATURE_PRODUCT_USB8741 uses AUDIO_PRODUCT_ID_7 and _8
## -DFEATURE_PRODUCT_AB1x uses AUDIO_PRODUCT_ID_9 and _10
## -DFEATURE_PRODUCT_AMB uses AUDIO_PRODUCT_ID_13 and _14
##
## Other defines:
## -DUSB_STATE_MACHINE_DEBUG activate audio feedback state machine
##                           debugging on GPIO and UART
##
## Hardware generation must be defined for Audio Widget, must define exactly one
## -DHW_GEN_AB1X uses the feature set for 
##   QNKTC AB-1.0, AB-1.1, AB-1.2, Henry Audio USB DAC 128 and USB DAC 128 mkII
## -DHW_GEN_DIN10 uses version 1.0 of the features for digital inputs and WM8805
##
## -DVDD_SENSE to enable bus-powered/self-powered configuration:
## - When AVR32_PIN_PA19 is 0, configure as bus-powered with 500mA max power
## - When AVR32_PIN_PA19 is 1, configure as self-powered with 10mA max power
## This data is sent to the host in the USB configuration descriptors.
##


## See features.h #define FEATURE_VALUE_NAMES for available defaults. 
#SDR_WIDGET_DEFAULTS=-DFEATURE_BOARD_DEFAULT=feature_board_widget \
#	-DFEATURE_IMAGE_DEFAULT=feature_image_uac2_dg8saq \
#	-DFEATURE_IN_DEFAULT=feature_in_normal \
#	-DFEATURE_OUT_DEFAULT=feature_out_normal \
#	-DFEATURE_ADC_DEFAULT=feature_adc_ak5394a \
#	-DFEATURE_DAC_DEFAULT=feature_dac_cs4344 \
#	-DFEATURE_LCD_DEFAULT=feature_lcd_hd44780 \
#	-DFEATURE_LOG_DEFAULT=feature_log_500ms \
#	-DFEATURE_FILTER_DEFAULT=feature_filter_fir \
#	-DFEATURE_QUIRK_DEFAULT=feature_quirk_none \
#	-DFEATURE_CFG_INTERFACE \
#	-DFEATURE_PRODUCT_SDR_WIDGET 

# These defaults are compiled into code, not necessarily forced
# into flash. To force them into flash, reboot with 
# feature_quirk_ptest set in flash, which will lead to flash being
# overwritten with defaults
AUDIO_WIDGET_DEFAULTS=-DFEATURE_BOARD_DEFAULT=feature_board_usbi2s \
	-DFEATURE_IMAGE_DEFAULT=feature_image_uac2_audio \
	-DFEATURE_IN_DEFAULT=feature_in_normal \
	-DFEATURE_OUT_DEFAULT=feature_out_normal \
	-DFEATURE_ADC_DEFAULT=feature_adc_none \
	-DFEATURE_DAC_DEFAULT=feature_dac_generic \
	-DFEATURE_LCD_DEFAULT=feature_lcd_none \
	-DFEATURE_LOG_DEFAULT=feature_log_none \
	-DFEATURE_FILTER_DEFAULT=feature_filter_fir \
	-DFEATURE_QUIRK_DEFAULT=feature_quirk_none \
	-DFEATURE_PRODUCT_AB1x \
	-DVDD_SENSE \
	-DUSB_STATE_MACHINE_GPIO \
	-DFEATURE_VOLUME_CTRL \
	-DHW_GEN_AB1X

# Loudness / USB statistics build options (defaults shown in `make help`)
LOUDNESS_TYPE ?= FAST
LOUDNESS_DISABLE ?= 0
USBSTATISTICS_DISABLE ?= 0

ifeq ($(LOUDNESS_TYPE),PRECISE)
  CFLAGS_LOUDNESS = -DPRECISE
else
  CFLAGS_LOUDNESS = -DFAST
endif
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

WIDGET_LOUDNESS_FLAGS = $(CFLAGS_LOUDNESS) $(CFLAGS_LOUDNESS_DISABLE) $(CFLAGS_LOUDNESS_USB_STATS_EVENTS)
AUDIO_WIDGET_CFLAGS = $(AUDIO_WIDGET_DEFAULTS) $(WIDGET_LOUDNESS_FLAGS)

# Choose wisely:
#   -DFEATURE_PRODUCT_AMB
#	-DFEATURE_PRODUCT_AB1x \
#	-DFEATURE_VOLUME_CTRL \
#	-DHW_GEN_AB1X \
# 	-DHW_GEN_DIN10 \
# 	-DHW_GEN_DIN20 \
#	-DFEATURE_CLOCK_SELECTOR \ - Build UAC2 with clock selector
#	-DUSB_STATE_MACHINE_DEBUG \ - Used for verbose RS232 debugging
#	-DUSB_STATE_MACHINE_GPIO \  - Used for 'scope debugging of state machine timing
#	-DFEATURE_LCD_DEFAULT=feature_lcd_hd44780 \
#	-DFEATURE_CFG_INTERFACE \						Enable the configuration interface at Endpoint 0. Disabling breaks UAC1
#	-DFEATURE_HID \


## Boot up with this code, reboot with feature_quirk_ptest set
## in flash (for good measure). That will execute the production 
## test with these defaults. Then reboot with feature_quirk_none
## and flash will not be overwritten with compiled-in defaults
## at next reboot.
#PROD_TEST_DEFAULTS=-DFEATURE_BOARD_DEFAULT=feature_board_usbi2s \
#	-DFEATURE_IMAGE_DEFAULT=feature_image_uac1_audio \
#	-DFEATURE_IN_DEFAULT=feature_in_normal \
#	-DFEATURE_OUT_DEFAULT=feature_out_normal \
#	-DFEATURE_ADC_DEFAULT=feature_adc_none \
#	-DFEATURE_DAC_DEFAULT=feature_dac_generic \
#	-DFEATURE_LCD_DEFAULT=feature_lcd_hd44780 \
#	-DFEATURE_LOG_DEFAULT=feature_log_500ms \
#	-DFEATURE_FILTER_DEFAULT=feature_filter_fir \ 
#	-DFEATURE_QUIRK_DEFAULT=feature_quirk_ptest \
#	-DVDD_SENSE \
#	-DHW_GEN_AB1X \
#	-DFEATURE_CFG_INTERFACE \
#	-DFEATURE_PRODUCT_AB1x

# ---------------------------------------------------------------------------
# PC unit test / host tool settings
# ---------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
  EXE_EXT = .exe
  VCPKG_DIR ?= C:/Users/AHysing/code/vcpkg
  VCPKG_INSTALLED = $(VCPKG_DIR)/installed/x64-windows
  ifeq ($(origin CC),default)
    CC = cl
  endif
  CC ?= cl
  USE_MSVC = $(findstring cl,$(CC))
else
  EXE_EXT =
  CC ?= gcc
  USE_MSVC =
endif

CFLAGS_TEST = -DBUILD_TESTING
TEST_BUILD_DIR = Release/tests/pc
VCVARS64 ?= C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat

ifneq ($(USE_MSVC),)
  TEST_PREAMBLE = /FItests/pc/compiler.h
else
  TEST_PREAMBLE = -include tests/pc/compiler.h
endif

TEST_CFLAGS = $(CFLAGS_LOUDNESS) $(CFLAGS_LOUDNESS_DISABLE) $(CFLAGS_LOUDNESS_USB_STATS_EVENTS)

ifdef MSYSTEM
  IS_MSYS = 1
endif

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
  LINK_USB = /link /LIBPATH:"$(VCPKG_INSTALLED)/lib" libusb-1.0.lib
  CFLAGS_COMMON = /nologo /I. /Isrc /Isrc/CONFIG /I"$(VCPKG_INSTALLED)/include" $(TEST_CFLAGS)
else
  OUT_FLAG = -o 
  OBJ_DIR_FLAG =
  LINK_USB = -L$(VCPKG_INSTALLED)/lib -lusb-1.0
  CFLAGS_COMMON = -I. -Isrc -I$(VCPKG_INSTALLED)/include $(TEST_CFLAGS)
endif

ifeq ($(OS),Windows_NT)
  RM = cmd /c del /f /q
  FIX_PATH = $(subst /,\,$(1))
else
  RM = rm -f
  FIX_PATH = $(1)
endif

ifdef IS_MSYS
  WIN_CURDIR = $(shell cygpath -m '$(CURDIR)')
endif

.PHONY: all test run-test run-test-all run-test-precise clean clean-test help \
	audio-widget sdr-widget build-audio-widget build-sdr-widget test-avr32

all:: Release/widget.elf widget-control$(EXE_EXT)

Release/widget.elf::
	rm -f Release/widget.elf Release/src/features.o
	CFLAGS="$(AUDIO_WIDGET_CFLAGS)" ./make-widget

audio-widget::
	rm -f Release/widget.elf Release/src/features.o
	CFLAGS="$(AUDIO_WIDGET_CFLAGS)" ./make-widget

#sdr-widget::
#	rm -f Release/widget.elf Release/src/features.o
#	CFLAGS="$(SDR_WIDGET_DEFAULTS)" ./make-widget

widget-control$(EXE_EXT): widget-control.c src/features.h
	$(CC) $(CFLAGS_COMMON) $(OUT_FLAG)$@ widget-control.c $(LINK_USB)

clean:: clean-test
	rm -f widget-control widget-control.exe
	cd Release && make clean

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

$(TEST_BUILD_DIR)/loudness_equalizer_step_switch_stats_tests$(EXE_EXT): tests/pc/loudness_equalizer_step_switch_stats_tests.c src/loudness.c src/usb_statistics.c src/stats_telemetry.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests/pc $(CFLAGS_COMMON) $(CFLAGS_TEST) -DUNIT_TEST $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_equalizer_step_switch_stats_tests.c src/loudness.c src/usb_statistics.c src/stats_telemetry.c

$(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT): tests/pc/loudness_tests.c src/loudness.c tests/pc/usb_volume_stub.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) -I tests/pc $(CFLAGS_COMMON) $(CFLAGS_TEST) -DUSBSTATISTICS_DISABLE $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/loudness_tests.c src/loudness.c tests/pc/usb_volume_stub.c

$(TEST_BUILD_DIR)/usb_statistics_tests$(EXE_EXT): tests/pc/usb_statistics_tests.c src/usb_statistics.c src/stats_telemetry.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) $(CFLAGS_COMMON) $(CFLAGS_TEST) -I tests/pc -DUNIT_TEST $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/usb_statistics_tests.c src/usb_statistics.c src/stats_telemetry.c

$(TEST_BUILD_DIR)/audio_stats_logic_tests$(EXE_EXT): tests/pc/audio_stats_logic_tests.c | $(TEST_BUILD_DIR)
	$(CC) $(TEST_PREAMBLE) $(CFLAGS_COMMON) $(CFLAGS_TEST) -I tests/pc $(OBJ_DIR_FLAG) $(OUT_FLAG)$@ tests/pc/audio_stats_logic_tests.c

RUN_TEST_EXES = $(TEST_BUILD_DIR)/audio_stats_logic_tests$(EXE_EXT) \
	$(TEST_BUILD_DIR)/usb_statistics_tests$(EXE_EXT)
ifneq ($(LOUDNESS_DISABLE),1)
RUN_TEST_EXES += $(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT)
ifneq ($(USBSTATISTICS_DISABLE),1)
RUN_TEST_EXES += $(TEST_BUILD_DIR)/loudness_equalizer_step_switch_stats_tests$(EXE_EXT)
endif
endif

test:
ifeq ($(OS),Windows_NT)
ifneq ($(USE_MSVC),)
ifdef IS_MSYS
	@cmd.exe //c run-pc-tests.cmd $(LOUDNESS_TYPE) $(LOUDNESS_DISABLE) $(USBSTATISTICS_DISABLE)
else
	@cmd /c run-pc-tests.cmd $(LOUDNESS_TYPE) $(LOUDNESS_DISABLE) $(USBSTATISTICS_DISABLE)
endif
else
	@$(MAKE) run-test LOUDNESS_TYPE=$(LOUDNESS_TYPE) LOUDNESS_DISABLE=$(LOUDNESS_DISABLE) USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)
endif
else
	@$(MAKE) run-test LOUDNESS_TYPE=$(LOUDNESS_TYPE) LOUDNESS_DISABLE=$(LOUDNESS_DISABLE) USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)
endif

run-test: $(RUN_TEST_EXES)
	$(TEST_BUILD_DIR)/audio_stats_logic_tests$(EXE_EXT)
ifneq ($(LOUDNESS_DISABLE),1)
ifneq ($(USBSTATISTICS_DISABLE),1)
	$(TEST_BUILD_DIR)/loudness_equalizer_step_switch_stats_tests$(EXE_EXT)
endif
	$(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT)
endif
	$(TEST_BUILD_DIR)/usb_statistics_tests$(EXE_EXT)

run-test-all: run-test
	@$(MAKE) run-test-precise LOUDNESS_TYPE=PRECISE LOUDNESS_DISABLE=$(LOUDNESS_DISABLE) USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)

ifneq ($(LOUDNESS_DISABLE),1)
run-test-precise: $(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT)
	$(TEST_BUILD_DIR)/loudness_tests$(EXE_EXT)
else
run-test-precise:
	@echo Skipping PRECISE loudness tests: LOUDNESS_DISABLE=1
endif

test-avr32: tests/avr32/statistics_avr32_tests.o

tests/avr32/statistics_avr32_tests.o: tests/avr32/statistics_avr32_tests.c
	"$(AVR32BIN)/avr32-gcc" -DBOARD=SDRwdgtLite -DFREERTOS_USED -Isrc/SOFTWARE_FRAMEWORK/UTILS/DEBUG -Isrc/SOFTWARE_FRAMEWORK/SERVICES/USB -Isrc/CONFIG -Isrc/SOFTWARE_FRAMEWORK/UTILS/PREPROCESSOR -Isrc/SOFTWARE_FRAMEWORK/UTILS -Isrc/SOFTWARE_FRAMEWORK/BOARDS -Isrc -mpart=uc3a3256 -c -o $@ $<

help:
	@echo "Firmware targets:"
	@echo "  make audio-widget              Build Release/widget.elf (AB1x defaults)"
	@echo "  make all                       widget.elf + widget-control.exe"
	@echo "  make clean                     Remove firmware and test build artifacts"
	@echo ""
	@echo "Loudness / statistics options (default: FAST, all features enabled):"
	@echo "  LOUDNESS_TYPE=FAST|PRECISE   Biquad path (default: FAST)"
	@echo "  LOUDNESS_DISABLE=1           Omit loudness filter from firmware"
	@echo "  USBSTATISTICS_DISABLE=1"
	@echo "                               Omit loudness equalizer-step USB events"
	@echo ""
	@echo "Examples:"
	@echo "  make audio-widget LOUDNESS_TYPE=PRECISE"
	@echo "  make audio-widget LOUDNESS_DISABLE=1"
	@echo "  make audio-widget USBSTATISTICS_DISABLE=1"
	@echo "  make test LOUDNESS_DISABLE=1"
	@echo "  make test USBSTATISTICS_DISABLE=1"
	@echo ""
	@echo "PC unit tests (MSVC on Windows):"
	@echo "  make test                      Build and run applicable test suites"
	@echo "  make run-test-all              FAST tests, then PRECISE loudness tests"
	@echo "  make clean-test                Remove Release/tests/pc"
	@echo ""
	@echo "Current settings:"
	@echo "  LOUDNESS_TYPE=$(LOUDNESS_TYPE)"
	@echo "  LOUDNESS_DISABLE=$(LOUDNESS_DISABLE)"
	@echo "  USBSTATISTICS_DISABLE=$(USBSTATISTICS_DISABLE)"
