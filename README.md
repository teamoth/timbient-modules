# Timbient Modules

A pixel modular patch-bay for ambient music: sequencers, voices and effects you patch together with cables.
This is the native version, written in C++. Inspired by SunVox.

**Status:** an early preview. It plays a built-in patch so the sound engine can be tested; the full interface is being ported from the web version.

## Download

Every change is built automatically. Get the newest version from the **Latest build** release:
https://github.com/teamoth/timbient-modules/releases/tag/latest

- **Mac** (Intel and Apple Silicon, macOS 10.13 or later): unzip, drag *Timbient Modules.app* into Applications, then
  run this once in Terminal (the app isn't signed with an Apple developer certificate):
  ```
  xattr -cr "/Applications/Timbient Modules.app"
  ```
- **Windows**: unzip and run *Timbient Modules.exe*. If Windows warns, click *More info*, then *Run anyway*.
- **Linux**: unpack and run `timbient-modules`.

## Building it yourself

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

SDL3 and miniaudio are downloaded automatically during the build.

## Layout

- `src/engine`: the sound engine and modules (plain C++, no libraries)
- `src/ui`: the pixel interface
- `src/app`: the window (SDL3) and the sound device (miniaudio)
- `tests`: offline tests that render patches to WAV files and the interface to images
