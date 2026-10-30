# Libretro core for Lupinho. The simulator stays in the lupinho/ submodule;
# this makefile only adds the frontend and the seams that must not live there.

LUPI := lupinho/src
LUA  := $(LUPI)/libs/linux/lua-5.5.0/src

ifeq ($(wildcard $(LUPI)/ui.c),)
$(error lupinho submodule is missing. Run: git submodule update --init)
endif

MINGW_CC ?= x86_64-w64-mingw32-gcc
WIN_SOFLAGS := -shared
WIN_LIBS := -lz -lm -static-libgcc

ifeq ($(OS),Windows_NT)
  EXT := dll
  SOFLAGS := $(WIN_SOFLAGS)
  LIBS := $(WIN_LIBS)
  LUAFLAGS :=
  RETROARCH := retroarch.exe
  CORE_DIR := $(APPDATA)/RetroArch/cores
  INFO_DIR := $(APPDATA)/RetroArch/info
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    EXT := dylib
    SOFLAGS := -dynamiclib
    LIBS := -lz -lm
    LUAFLAGS := -DLUA_USE_POSIX
    RETROARCH := /Applications/RetroArch.app/Contents/MacOS/RetroArch
    CORE_DIR := $(HOME)/Library/Application Support/RetroArch/cores
    INFO_DIR := $(HOME)/Library/Application Support/RetroArch/info
  else
    EXT := so
    SOFLAGS := -shared
    LIBS := -lz -lm -ldl
    LUAFLAGS := -DLUA_USE_POSIX
    RETROARCH := retroarch
    CORE_DIR := $(HOME)/.config/retroarch/cores
    INFO_DIR := $(HOME)/.config/retroarch/info
  endif
endif

CC ?= gcc
CFLAGS := -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -O2 -fPIC $(LUAFLAGS)
CPPFLAGS := -Isrc/compat -Isrc -I$(LUPI) -I$(LUA)

CORE_FILE := lupinho_libretro.$(EXT)
OUTPUT := dist/$(CORE_FILE)
CORE_PATH := $(CORE_DIR)/$(CORE_FILE)

CORE_SRCS := libretro.c input.c pointer.c video.c palette.c zip.c raylib_shim.c
CORE_OBJS := $(addprefix build/,$(CORE_SRCS:.c=.o))

LUA_SRCS := \
	lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c \
	lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c \
	lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c \
	lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c
LUA_OBJS := $(addprefix build/lua/,$(LUA_SRCS:.c=.o))

OBJS := $(CORE_OBJS) build/ui.o build/lua_api.o $(LUA_OBJS)

.PHONY: all libretro libretro-win libretro-android libretro-android-all clean install/core get/game run

all: libretro

libretro: $(OUTPUT)

$(OUTPUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(SOFLAGS) -o $@ $(OBJS) $(LIBS)
	cp -f src/lupinho_libretro.info dist/lupinho_libretro.info

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/lua_api.o: $(LUPI)/lua_api.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Keep lupinho's palset symbol out of the link. palette.c supplies the
# BGR555 channel order used by lupi-codec.
build/ui.o: $(LUPI)/ui.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dpalset=lupi_core_palset -c $< -o $@

build/lua/%.o: $(LUA)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

libretro-win:
	$(MAKE) all CC="$(MINGW_CC)" EXT=dll SOFLAGS="$(WIN_SOFLAGS)" LIBS="$(WIN_LIBS)" LUAFLAGS=""

# Cross-compile with the Android NDK. ANDROID_NDK is the NDK root.
# ANDROID_ABI is one of arm64-v8a, armeabi-v7a, x86_64, x86.
# Output is dist/android/<abi>/lupinho_libretro_android.so, the name RetroArch loads.
# API 24: 32-bit ABIs use 64-bit off_t, and bionic only declares fseeko/ftello
# for that mode from Android 7. arm64 links the same API so every core matches.
ANDROID_API ?= 24
ANDROID_ABI ?= arm64-v8a
ANDROID_ABIS ?= arm64-v8a armeabi-v7a x86_64 x86
ANDROID_SOFLAGS := -shared -Wl,--no-undefined -Wl,-z,max-page-size=16384
ANDROID_LIBS := -lz -lm

libretro-android:
	@set -eu; \
	test -n "$(ANDROID_NDK)" || { echo "ANDROID_NDK is not set" >&2; exit 1; }; \
	abi="$(ANDROID_ABI)"; api="$(ANDROID_API)"; ndk="$(ANDROID_NDK)"; \
	case "$$abi" in \
	  arm64-v8a) triple=aarch64-linux-android ;; \
	  armeabi-v7a) triple=armv7a-linux-androideabi ;; \
	  x86_64) triple=x86_64-linux-android ;; \
	  x86) triple=i686-linux-android ;; \
	  *) echo "unknown ANDROID_ABI: $$abi" >&2; exit 1 ;; \
	esac; \
	prebuilt="$$ndk/toolchains/llvm/prebuilt"; \
	os=`uname -s`; mach=`uname -m`; \
	case "$$os" in \
	  Darwin) \
	    if [ "$$mach" = "arm64" ] && [ -x "$$prebuilt/darwin-arm64/bin/clang" ]; then host=darwin-arm64; \
	    else host=darwin-x86_64; fi ;; \
	  Linux) host=linux-x86_64 ;; \
	  *) echo "unsupported host for Android build: $$os" >&2; exit 1 ;; \
	esac; \
	cc="$$prebuilt/$$host/bin/$${triple}$${api}-clang"; \
	strip="$$prebuilt/$$host/bin/llvm-strip"; \
	test -x "$$cc" || { echo "NDK clang not found: $$cc" >&2; exit 1; }; \
	jobs=`nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4`; \
	rm -rf build; \
	$(MAKE) -j"$$jobs" all \
	  CC="$$cc" \
	  EXT=so \
	  OUTPUT="dist/android/$$abi/lupinho_libretro_android.so" \
	  SOFLAGS="$(ANDROID_SOFLAGS)" \
	  LIBS="$(ANDROID_LIBS)" \
	  LUAFLAGS="-DLUA_USE_POSIX"; \
	cp -f src/lupinho_libretro.info "dist/android/$$abi/lupinho_libretro.info"; \
	"$$strip" --strip-unneeded "dist/android/$$abi/lupinho_libretro_android.so"

libretro-android-all:
	rm -rf dist/android
	@set -e; for abi in $(ANDROID_ABIS); do \
	  $(MAKE) libretro-android ANDROID_ABI="$$abi" ANDROID_NDK="$(ANDROID_NDK)" ANDROID_API="$(ANDROID_API)"; \
	done

clean:
	rm -rf build dist

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
GAME_URL = https://github.com/lupi-org-br/caio-pernocas
GAME_CODEC_DIR_IN = $(MAKEFILE_DIR)dist/game
GAME_CODEC_DIR_OUT = $(MAKEFILE_DIR)dist/game-release

core/install: $(OUTPUT)
	mkdir -p "$(CORE_DIR)" "$(INFO_DIR)"
	cp -f $(OUTPUT) "$(CORE_PATH)"
	cp -f dist/lupinho_libretro.info "$(INFO_DIR)/lupinho_libretro.info"

game/clone:
	git clone --depth 1 "$(GAME_URL)" dist/game

game/codec:
	cd lupi-codec && \
	lua run.lua "$(GAME_CODEC_DIR_IN)" "$(GAME_CODEC_DIR_OUT)"

retroarch/run:
	$(RETROARCH) -L "$(CORE_PATH)" -v "$(GAME_CODEC_DIR_OUT)/current"
