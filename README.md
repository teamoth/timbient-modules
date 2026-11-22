# Timbient Modules

A friendly, pixelated, modular patch-bay environment for ambient music, with a variety of sequencers, voices and effects to patch together.

Written in C++ and inspired by the idea of having something that's a bit like a cross between SunVox a Monome Norns. Where this is different from a DAW is that there isn't just one BPM and it's not really about using specific notes. It is possible to clock sync everything together (and there is a note entry tracker module), but the idea here is that the separate voices can have completely different timings from each other. 

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

## Web player

To listen to a session on a phone, a tablet or any computer without installing anything, open
**https://teamoth.github.io/timbient-modules/** and open a session file (.tmbnt, saved from the app with SAVE or SAVE AS:
its sounds go inside it). It plays sessions only; it doesn't change them. Sessions up to 50 MB open there.

It can be installed from the browser's menu (**Install app**, or **Add to Home Screen**), and then opens full screen from
the home screen, like an app. While a session plays the controls tuck away after a few seconds: tap the session to bring
them back. In a browser, **FULL SCREEN** hides the browser's own bars.

## Notes and names

**NOTES** (in + ADD, under PROCESS) is a box to type into: set notes, reminders, what a MIDI knob does. Click in it to type;
Escape or a click elsewhere when done. Drag its bottom-right corner to resize it; folded, it shows its first line, so a first
line makes a label. Notes are saved with the session, and show (but can't be changed) in the web player.

When naming a session (SAVE AS), **ADD KEY AND SCALE** puts the session's key and scale in the file name:
"calm example (db_yo)". An older bracket like it is replaced rather than added to.

## Blending between sessions

**SESSIONS > BLEND TO...** picks the next session. It loads while the current one plays on, then fades in as the current one
fades out, keeping the overall level steady (**BLEND LENGTH**: 10 seconds to 4 minutes). The screen shows the new session
straight away, so it can be adjusted as it rises; its tempo glides from the old session's to its own. **NOW** in the top bar
finishes the blend in a few seconds, **CANCEL** fades back to the session you came from, just as it was. A recording carries
straight on through a blend.

**PAUSE** holds a blend where it is (**GO** carries on). Drag the bar to move the blend by hand; it waits wherever you leave it,
and resting at the far end for 2 seconds finishes it. Right-click the bar for **MIDI LEARN**, to move it with a knob or slider
(it picks up where the blend is). **BLEND LENGTH > MANUAL** makes blends move only by hand. On a Mac with a Touch Bar, the
Touch Bar becomes the blend's crossfader, with PAUSE, NOW and CANCEL.

The middle of the top bar shows the session's name (**NEW SESSION** until it's first saved), with a dot when it has changes
not yet saved.

## MIDI controllers

Right-click (or Ctrl-click) any control and choose **MIDI LEARN**, then move a knob, fader or encoder, or press a button,
on your controller. Knobs take over once they reach the control's current value (so nothing jumps), encoders sending
turns are recognised, as are fine 14-bit controls and NRPN, and values go back to the controller so encoders and
displays match the screen. **SETTINGS > MIDI > MIDI MAP** lists every mapping. Mappings are saved with the session, or can be
kept for every session.

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

Designed by Tim Medlicott, built with Claude (Anthropic's AI).

The MI VOICES module, and the RINGS types of PLUCK STRING and RESONATOR, use the original open-source code of Mutable Instruments' Plaits and Rings, by Emilie Gillet (MIT licence).
Timbient Modules also uses SDL 3 (zlib licence), miniaudio (public domain / MIT No Attribution) and RtMidi (MIT licence).
Everything else was written for Timbient Modules. Timbient Modules is not made by or affiliated with Mutable Instruments.
The full notices are in [CREDITS.md](CREDITS.md), which also comes inside each download.
