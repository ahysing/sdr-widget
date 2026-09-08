# -*- mode: makefile; tab-width: 4; -*-
#
# Standalone Makefile for henryctl (Host CLI for Henry Audio / SDR-Widget)
# Supports:
#   - MSYS2 / UCRT64 (GCC with static libusb from pacman)
#   - Windows PowerShell / MSVC (cl with static libusb from vcpkg: x64-windows-static)
#   - Linux (GCC with static/dynamic libusb)

ifdef MSYSTEM
  IS_MSYS = 1
endif

ifeq ($(OS),Windows_NT)
  EXE_EXT = .exe
  VCPKG_DIR ?= C:/Users/AHysing/code/vcpkg
  VCPKG_INSTALLED ?= $(VCPKG_DIR)/installed/x64-windows-static
  ifdef IS_MSYS
    # MSYS2 (UCRT64/MINGW64): use GCC + static libusb from pacman
    CC ?= gcc
    USE_MSVC =
  else
    ifeq ($(origin CC),default)
      CC = cl
    endif
    CC ?= cl
    USE_MSVC = $(findstring cl,$(CC))
  endif
else
  EXE_EXT =
  CC ?= gcc
  USE_MSVC =
endif

# Compiler & Linker configuration
ifneq ($(USE_MSVC),)
  # MSVC (PowerShell / Developer Command Prompt)
  LIBUSB_INCLUDE ?= $(VCPKG_INSTALLED)/include
  LIBUSB_LIBDIR ?= $(VCPKG_INSTALLED)/lib
  LIBUSB_HEADER = $(LIBUSB_INCLUDE)/libusb-1.0/libusb.h
  CFLAGS ?= /nologo /O2
  INCLUDES = /I. /I.. /I../src /I"$(LIBUSB_INCLUDE)"
  OUT_FLAG = /Fe:
  LINK_USB = /link /LIBPATH:"$(LIBUSB_LIBDIR)" libusb-1.0.lib
  COMPILE_CMD = $(CC) $(CFLAGS) $(INCLUDES) $(OUT_FLAG)$@ henryctl.c $(LINK_USB)
  RM = cmd /c del /f /q 2>nul || true
else ifdef IS_MSYS
  # MSYS2 (GCC with static libusb linking)
  MSYS_PREFIX := $(shell dirname $$(dirname $$(which $(CC) 2>/dev/null)))
  LIBUSB_INCLUDE ?= $(MSYS_PREFIX)/include
  LIBUSB_LIBDIR ?= $(MSYS_PREFIX)/lib
  LIBUSB_HEADER = $(LIBUSB_INCLUDE)/libusb-1.0/libusb.h
  CFLAGS ?= -O2 -Wall
  INCLUDES = -I. -I.. -I../src -I$(LIBUSB_INCLUDE)
  OUT_FLAG = -o 
  # Link libusb statically to avoid runtime dependency on libusb-1.0.dll
  LINK_USB = -L$(LIBUSB_LIBDIR) -Wl,-Bstatic -lusb-1.0 -Wl,-Bdynamic -lsetupapi
  COMPILE_CMD = $(CC) $(CFLAGS) $(INCLUDES) $(OUT_FLAG)$@ henryctl.c $(LINK_USB)
  RM = rm -f
else
  # Linux (GCC)
  PKG_CONFIG ?= pkg-config
  LIBUSB_CFLAGS := $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
  LIBUSB_LIBS := $(shell $(PKG_CONFIG) --static --libs libusb-1.0 2>/dev/null || $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null || echo "-lusb-1.0 -lpthread -ludev")
  LIBUSB_HEADER = /usr/include/libusb-1.0/libusb.h
  CFLAGS ?= -O2 -Wall
  INCLUDES = -I. -I.. -I../src $(LIBUSB_CFLAGS)
  OUT_FLAG = -o 
  LINK_USB = $(LIBUSB_LIBS)
  COMPILE_CMD = $(CC) $(CFLAGS) $(INCLUDES) $(OUT_FLAG)$@ henryctl.c $(LINK_USB)
  RM = rm -f
endif

.PHONY: all clean help check-libusb henryctl

all: henryctl$(EXE_EXT)

henryctl: henryctl$(EXE_EXT)

ifneq ($(EXE_EXT),.exe)
henryctl.exe: henryctl$(EXE_EXT)
endif

check-libusb:
ifneq ($(USE_MSVC),)
ifeq ($(wildcard $(LIBUSB_HEADER)),)
	@echo "libusb headers not found at $(LIBUSB_HEADER)"
	@echo "PowerShell / MSVC prerequisite:"
	@echo "  vcpkg install libusb:x64-windows-static"
	@echo "  make VCPKG_DIR=<path-to-vcpkg>"
	@exit 1
endif
else ifdef IS_MSYS
ifeq ($(wildcard $(LIBUSB_HEADER)),)
	@echo "libusb headers not found at $(LIBUSB_HEADER)"
	@echo "MSYS2 prerequisite:"
	@echo "  pacman -S --needed mingw-w64-ucrt-x86_64-gcc make mingw-w64-ucrt-x86_64-libusb"
	@exit 1
endif
endif

henryctl$(EXE_EXT): henryctl.c | check-libusb
	$(COMPILE_CMD)

clean:
ifeq ($(OS),Windows_NT)
	-cmd /c "del /f /q henryctl henryctl.exe *.obj *.o 2>NUL"
else
	rm -f henryctl henryctl.exe *.o *.obj
endif

help:
	@echo "henryctl standalone build targets:"
	@echo "  make                 Build henryctl binary"
	@echo "  make henryctl.exe    Build henryctl.exe binary"
	@echo "  make clean           Remove build artifacts"
	@echo ""
	@echo "Prerequisites:"
	@echo "  MSYS2:      pacman -S mingw-w64-ucrt-x86_64-gcc make mingw-w64-ucrt-x86_64-libusb"
	@echo "  PowerShell: vcpkg install libusb:x64-windows-static"
	@echo "  Linux:      sudo apt install libusb-1.0-0-dev"
