# Libretro core for Lupinho. The simulator stays in the lupinho/ submodule;
# this makefile only adds the frontend and the seams that must not live there.

LUPI := lupinho/src
LUA  := $(LUPI)/libs/linux/lua-5.5.0/src

ifeq ($(wildcard $(LUPI)/ui.c),)
$(error lupinho submodule is missing. Run: git submodule update --init)
endif

# The win32 thread model ships a libstdc++ without std::thread. Furnace's log uses it.
MINGW_CC ?= x86_64-w64-mingw32-gcc-posix
WIN_SOFLAGS := -shared
WIN_LIBS := -lz -lm -static-libgcc -static-libstdc++
PTHREAD_LINK ?= -pthread

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

# C++ driver so Furnace's static initializers and libstdc++/libc++ are linked.
ifeq ($(findstring clang,$(CC)),clang)
  CXX := $(subst clang,clang++,$(CC))
else ifeq ($(findstring gcc,$(CC)),gcc)
  CXX := $(subst gcc,g++,$(CC))
else ifeq ($(UNAME_S),Darwin)
  CXX := c++
else
  CXX := g++
endif

CORE_FILE := lupinho_libretro.$(EXT)
OUTPUT := dist/$(CORE_FILE)
CORE_PATH := $(CORE_DIR)/$(CORE_FILE)

CORE_SRCS := libretro.c input.c pointer.c video.c palette.c zip.c raylib_shim.c blit.c music.c
CORE_OBJS := $(addprefix build/,$(CORE_SRCS:.c=.o))

LUA_SRCS := \
	lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c \
	lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c \
	lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c \
	lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c
LUA_OBJS := $(addprefix build/lua/,$(LUA_SRCS:.c=.o))

OBJS := $(CORE_OBJS) build/ui.o build/lua_api.o $(LUA_OBJS)

# Same Furnace revision Lupinho plays. Docker passes FURNACE_SRC=/furnace.
FURNACE_SRC ?= $(abspath ../furnace)
FURNACE_TAG ?= host
FURNACE_BUILD := $(LUPI)/music/furnace-build-$(FURNACE_TAG)
FURNACE_LIB := $(FURNACE_BUILD)/libfurengine.a
FUR_PLAYER_OBJ := $(LUPI)/music/fur_player-$(FURNACE_TAG).o
FUR_CXXFLAGS := -std=c++14 -fPIC -pthread -DHAVE_SNDFILE \
	-include cstring -include climits \
	-I$(FURNACE_SRC)/src -I$(FURNACE_SRC)/src/engine \
	-I$(FURNACE_SRC)/extern/fmt/include \
	-I$(FURNACE_SRC)/extern/blip_buf \
	-I$(FURNACE_SRC)/extern/libsndfile-modified/include
FURNACE_CMAKE_EXTRA ?=
# Headless. Render backends pull GL, Metal, DirectX, which this core does not use.
FURNACE_CMAKE_HEADLESS := \
	-DWITH_RENDER_SDL=OFF -DWITH_RENDER_OPENGL=OFF -DWITH_RENDER_OPENGL1=OFF \
	-DWITH_RENDER_DX11=OFF -DWITH_RENDER_DX9=OFF -DWITH_RENDER_METAL=OFF \
	-DWITH_ASIO=OFF

ifeq ($(findstring dynamiclib,$(SOFLAGS)),dynamiclib)
  FUR_WHOLE := -Wl,-force_load,$(FURNACE_LIB)
else
  FUR_WHOLE := -Wl,--whole-archive $(FURNACE_LIB) -Wl,--no-whole-archive
endif

.PHONY: all docker libretro libretro-universal libretro-win libretro-android libretro-android-all clean install/core get/game run

all: libretro

# Same build entry point locally and in GitHub Actions. Export only artifacts;
# compilation happens inside Docker, without reusing the host's object files.
PLATFORM ?= linux
DOCKER ?= docker

# osxcross is a linux/amd64 image. On an Apple Silicon Mac that build runs under
# emulation and compiles Furnace twice, so the Mac builds the universal core
# with the host clang instead.
ifeq ($(shell uname -m),arm64)
MACOS_HOST_ARCH := arm64
MACOS_OTHER_ARCH := x86_64
else
MACOS_HOST_ARCH := x86_64
MACOS_OTHER_ARCH := arm64
endif

libretro-universal:
	rm -rf build
	$(MAKE) libretro \
	  CC="clang -arch $(MACOS_HOST_ARCH)" \
	  EXT=dylib SOFLAGS=-dynamiclib LIBS="-lz -lm" LUAFLAGS=-DLUA_USE_POSIX \
	  FURNACE_TAG=host \
	  OUTPUT=dist/lupinho_libretro-$(MACOS_HOST_ARCH).dylib
	rm -rf build
	$(MAKE) libretro \
	  CC="clang -arch $(MACOS_OTHER_ARCH)" \
	  EXT=dylib SOFLAGS=-dynamiclib LIBS="-lz -lm" LUAFLAGS=-DLUA_USE_POSIX \
	  FURNACE_TAG=macos-$(MACOS_OTHER_ARCH) \
	  FURNACE_CMAKE_EXTRA="-DCMAKE_OSX_ARCHITECTURES=$(MACOS_OTHER_ARCH)" \
	  OUTPUT=dist/lupinho_libretro-$(MACOS_OTHER_ARCH).dylib
	lipo -create \
	  dist/lupinho_libretro-$(MACOS_HOST_ARCH).dylib \
	  dist/lupinho_libretro-$(MACOS_OTHER_ARCH).dylib \
	  -output dist/lupinho_libretro.dylib
	rm -f dist/lupinho_libretro-arm64.dylib dist/lupinho_libretro-x86_64.dylib

docker:
	@case "$(PLATFORM)" in linux|macos|windows|android) ;; \
	  *) echo "unknown PLATFORM: $(PLATFORM) (linux, macos, windows, android)" >&2; exit 1 ;; esac
ifeq ($(PLATFORM)$(UNAME_S),macosDarwin)
	$(MAKE) libretro-universal
else
	$(DOCKER) buildx build --platform linux/amd64 --file Dockerfile.$(PLATFORM) --output type=local,dest=dist .
endif

libretro: $(OUTPUT)

$(FURNACE_LIB):
	@test -f "$(FURNACE_SRC)/src/engine/engine.h" || { \
	  echo "Furnace sources not found at $(FURNACE_SRC)" >&2; \
	  echo "Clone https://github.com/tildearrow/furnace or set FURNACE_SRC." >&2; \
	  exit 1; }
	mkdir -p $(FURNACE_BUILD)
	printf '%s\n' \
	  'set(CMAKE_C_COMPILE_OPTIONS_PIE -fPIC)' \
	  'set(CMAKE_CXX_COMPILE_OPTIONS_PIE -fPIC)' \
	  '# Android marks the headless player as a mobile app, which then requires SDL2.' \
	  'add_compile_options(-UIS_MOBILE)' \
	  > $(FURNACE_BUILD)/force-pic.cmake
	rm -f $(FURNACE_LIB) $(FURNACE_BUILD)/furengine-libs.txt
	sh $(LUPI)/music/build_furnace.sh $(FURNACE_TAG) "$(FURNACE_SRC)" \
	  $(FURNACE_CMAKE_HEADLESS) $(FURNACE_CMAKE_EXTRA) \
	  -DCMAKE_PROJECT_INCLUDE=$(abspath $(FURNACE_BUILD))/force-pic.cmake \
	  -DSYSTEM_ZLIB=OFF \
	  || test -s $(FURNACE_LIB)
	python3 src/furnace_link_libs.py $(FURNACE_BUILD)

$(FUR_PLAYER_OBJ): $(LUPI)/music/fur_player.cpp $(LUPI)/music/fur_player.h $(FURNACE_LIB)
	$(CXX) -c $(LUPI)/music/fur_player.cpp -o $@ $(FUR_CXXFLAGS)

$(OUTPUT): $(OBJS) $(FURNACE_LIB) $(FUR_PLAYER_OBJ)
	@mkdir -p $(dir $@)
	@libs=`cat $(FURNACE_BUILD)/furengine-libs.txt`; \
	fixed=""; \
	prev=""; \
	for lib in $$libs; do \
	  if [ "$$prev" = "-framework" ]; then \
	    fixed="$$fixed -framework $$lib"; \
	    prev=""; \
	    continue; \
	  fi; \
	  case $$lib in \
	    -framework) prev="-framework" ;; \
	    -*) fixed="$$fixed $$lib" ;; \
	    /*) fixed="$$fixed $$lib" ;; \
	    *) fixed="$$fixed $(LUPI)/$$lib" ;; \
	  esac; \
	done; \
	$(CXX) $(SOFLAGS) -o $@ $(OBJS) $(FUR_PLAYER_OBJ) $(FUR_WHOLE) $$fixed $(LIBS) $(PTHREAD_LINK)
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
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dpalset=lupi_core_palset \
		-Ddraw_tile=lupi_core_draw_tile_unused \
		-Ddraw_spr=lupi_core_draw_spr_unused \
		-Ddraw_map_layer=lupi_core_draw_map_layer_unused \
		-c $< -o $@

build/lua/%.o: $(LUA)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

libretro-win:
	$(MAKE) all CC="$(MINGW_CC)" EXT=dll SOFLAGS="$(WIN_SOFLAGS)" LIBS="$(WIN_LIBS)" LUAFLAGS="" \
	  PTHREAD_LINK="-Wl,-Bstatic -lwinpthread" \
	  FURNACE_TAG=windows \
	  FURNACE_CMAKE_EXTRA="-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=$(MINGW_CC) -DCMAKE_CXX_COMPILER=$(subst gcc,g++,$(MINGW_CC)) -DSYSTEM_ZLIB=OFF"

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
	  LUAFLAGS="-DLUA_USE_POSIX" \
	  FURNACE_TAG="android-$$abi" \
	  FURNACE_CMAKE_EXTRA="-DCMAKE_TOOLCHAIN_FILE=$$ndk/build/cmake/android.toolchain.cmake -DANDROID_ABI=$$abi -DANDROID_PLATFORM=android-$$api -DANDROID_STL=c++_static -DSYSTEM_ZLIB=OFF -DHAVE_FSEEKO=ON"; \
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
