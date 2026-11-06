# Timbient Modules

A modular patch-bay for ambient music with simplistic pixelated graphics: Sequencers, voices and effects you patch together with cables.
Written in C++, inspired by the idea of having something that's like SunVox and Norns scripts all mashed together. Where this is different from SunVox is that there isn't one BPM and it's not about specific notes that you enter. It is possible to sync things together and there is a tracker module if you do want to sequence specific notes, but the idea here is that you have things more random/generative and out sync from each other. You choose a scale, you choose a key, you choose a sequencer style and connect some modules together. In this way it's more like a modular synthesizer. One goal here is to have this running well on older hardware, although at the moment I'm limited by what devices I own.

I've had a lot of help from Claude to build this and the intention is that it's free for anyone to use.

**Status:** An early preview. Things are still being developed, but it does work. Improvements are onging. Testing is happening on Windows (Surface Go 2) and Mac (2017 MBP). Currently untested on Linux.

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
