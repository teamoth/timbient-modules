# Timbient Modules

A friendly, pixelated, modular patch-bay environment for ambient music, with a variety of sequencers, voices and effects to patch together.

Written in C++ and inspired by the idea of having something that's like SunVox and various Norns scripts all mashed together. Where this is different from a DAW is that there isn't just one BPM and it's not about using specific notes. It is possible to clock sync everything together (and there is a note entry tracker module), but the idea here is that the separate voices can have completely different timings from each other. 

One goal here is to have this running well on older hardware, for example 32bit windows machines.

**Status:** An early preview. Things are still being developed, but it does work. Improvements are onging. Testing is happening on:

- Windows 64bit (Surface Go 2)
- Windows 32bit (Panasonic CF-U1)
- Mac (2017 MBP)
- Linux - currently untested

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

## Credits

The MI VOICES module uses the original open-source code of Mutable Instruments' Plaits, by Emilie Gillet (MIT licence).
Timbient Modules also uses SDL 3, miniaudio and RtMidi. Timbient Modules is not made by or affiliated with Mutable Instruments.
The full notices are in [CREDITS.md](CREDITS.md), which also comes inside each download.
