[![release](https://github.com/humbertodias/lupinho-libretro/actions/workflows/release.yml/badge.svg)](https://github.com/humbertodias/lupinho-libretro/actions/workflows/release.yml)
![GitHub all releases](https://img.shields.io/github/downloads/humbertodias/lupinho-libretro/total)

# Lupinho Libretro

Libretro core for [Lupinho](https://github.com/lupi-org-br/lupinho), the simulator for the Lupi fantasy console. Load a `.lupi` game, or `game.lua` inside a game directory, in RetroArch.

## Build

```bash
git clone https://github.com/humbertodias/lupinho-libretro.git
cd lupinho-libretro
git submodule update --init
make docker PLATFORM=linux
```

Requires Docker with Buildx and make. Local builds and GitHub releases use the same `Dockerfile.$PLATFORM`:

```bash
make docker PLATFORM=linux
make docker PLATFORM=macos
make docker PLATFORM=windows
make docker PLATFORM=android
```

Artifacts are exported to `dist/`: `lupinho_libretro.so` (Linux), `.dylib` (macOS), `.dll` (Windows), plus `lupinho_libretro.info`. Linux and Windows builds target x86_64; macOS builds a universal binary (x86_64 + arm64) using [osxcross](https://github.com/crazy-max/docker-osxcross). Android builds all four ABIs (`arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86`) into `dist/android/` and `dist/lupinho_libretro_android.zip`. On ARM hosts, Docker must support `linux/amd64` emulation.

For a native build without Docker, run `make libretro` with a C99 compiler, make and zlib installed.

## Core

Install the core into RetroArch's cores directory and `lupinho_libretro.info` into its info directory:

```bash
make core/install
```

## Game

Clone a game into `dist/game`. `GAME_URL` defaults to caio-pernocas; pass another repo to override it:

```bash
make game/clone GAME_URL=https://github.com/lupi-org-br/caio-pernocas
```

Convert that game to the console release format. Output goes to `dist/game-release` with `current` pointing at the latest release. Requires `Lua` and `ImageMagick`.

```bash
make game/codec
```

## RetroArch

Open RetroArch with this core and `dist/game-release/current`:

```bash
make retroarch/run
```

<img width="1552" height="950" alt="Lupinho running in RetroArch" src="https://github.com/user-attachments/assets/6b51f732-0ed8-4019-888e-8028058782f0" />

<details>
  <summary>Joystick</summary>

| Retropad | Lupi |
| --- | --- |
| D-pad / left stick | UP, DOWN, LEFT, RIGHT |
| A | BTN_X |
| B | BTN_Z |
| X | BTN_Q |
| Y | BTN_E |
| L | BTN_F |
| R | BTN_G |
| Start / Select | ? / ? |

</details>
