[![release](https://github.com/humbertodias/lupinho-libretro/actions/workflows/release.yml/badge.svg)](https://github.com/humbertodias/lupinho-libretro/actions/workflows/release.yml)
![GitHub all releases](https://img.shields.io/github/downloads/humbertodias/lupinho-libretro/total)

# Lupinho Libretro

Libretro core for [Lupinho](https://github.com/lupi-org-br/lupinho), the simulator for the Lupi fantasy console. Load a `.lupi` game, or `game.lua` inside a game directory, in RetroArch.

## Build

```bash
git clone --recurse-submodules https://github.com/humbertodias/lupinho-libretro.git
cd lupinho-libretro
make libretro
```

This writes `dist/lupinho_libretro.so` (Linux), `.dylib` (macOS) or `.dll` (Windows), plus `dist/lupinho_libretro.info`.

Requires a C99 compiler, make and zlib. If the clone skipped submodules:

```bash
git submodule update --init
```

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


