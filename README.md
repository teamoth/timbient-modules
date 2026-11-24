# Timbient Modules

A pixelated, modular patch-bay playground environment for ambient/experimental music making, with a variety of sequencers, voices and effects to patch together. Written in C++. Where this is different from a DAW is that there isn't just one BPM and you don't need to input specific notes. It is possible to clock sync everything together (and there is a note entry tracker module), but the idea here is that the separate voices can have completely different timings from each other and you can choose a scale, key and range in which notes are generated. 

One goal here is to have this running well on older hardware, for example 32bit windows machines.

## Download

Every change is built automatically. Get the newest version from the **Latest build** release:
https://github.com/teamoth/timbient-modules/releases/tag/latest

- **Mac** (Intel and Apple Silicon, macOS 10.13 or later): unzip, drag *Timbient Modules.app* into Applications, then
  **run this once in Terminal (the app isn't signed with an Apple developer certificate):**
  ```
  xattr -cr "/Applications/Timbient Modules.app"
  ```
- **Windows**: unzip and run *Timbient Modules.exe*. **If Windows warns**, click *More info*, then ***Run anyway***.
- **Linux**: unpack and run `timbient-modules`.

## Blending between sessions

**SESSIONS > BLEND TO...** picks the next session. It loads while the current one plays on, then fades in as the current one
fades out, keeping the overall level steady (**BLEND LENGTH**: 10 seconds to 4 minutes). The screen shows the new session
straight away, so it can be adjusted as it rises; its tempo glides from the old session's to its own.

## MIDI controllers

Right-click (or Ctrl-click) any control and choose **MIDI LEARN**, then move a knob, fader or encoder, or press a button,
on your controller. Knobs take over once they reach the control's current value (so nothing jumps), encoders sending
turns are recognised, as are fine 14-bit controls and NRPN, and values go back to the controller so encoders and
displays match the screen. **SETTINGS > MIDI > MIDI MAP** lists every mapping. Mappings are saved with the session, or can be
kept for every session.

## Layout

- `src/engine`: the sound engine and modules (plain C++, no libraries)
- `src/ui`: the pixel interface
- `src/app`: the window (SDL3) and the sound device (miniaudio)
- `tests`: offline tests that render patches to WAV files and the interface to images

## Web player

To listen to a session on a phone, a tablet or any computer without installing anything, open
https://teamoth.github.io/timbient-modules/player/ and load up a session file. At this stage it only plays sessions (so they can't be edited). I might add more functionality to the web player in time. Session files up to 50 MB (that would be a lot of samples!) can be opened using the Web player.

## Credits

Designed by Tim Medlicott, built with Claude (Anthropic's AI).

The MI VOICES module, and the RINGS types of PLUCK STRING and RESONATOR, use the original open-source code of Mutable Instruments' Plaits and Rings, by Emilie Gillet (MIT licence).
Timbient Modules also uses SDL 3 (zlib licence), miniaudio (public domain / MIT No Attribution) and RtMidi (MIT licence).
Everything else was written for Timbient Modules. Timbient Modules is not made by or affiliated with Mutable Instruments.
The full notices are in [CREDITS.md](CREDITS.md), which also comes inside each download.
