<!--
  The words on the home page (teamoth.github.io/timbient-modules). Edit them here, like the README:
  - The line starting with # is the page's title; the paragraph under it is the line beneath the title, and the
    paragraph after that is the small line under the download buttons.
  - Each ## starts a section (a box on the page); the sections sit side by side, then stack on a phone.
  - **bold**, *italic*, [link text](https://address), and a blank line between paragraphs.
  - A block between two lines of ``` is shown as code (like the Terminal command).
  The download buttons and the screenshot are part of the page's layout, not this file.
  Saving this file rebuilds the page: it's live about 10-15 minutes later. (This note doesn't show.)
-->

# timbient modules

A pixelated, modular patch-bay playground for ambient and experimental music: sequencers, voices and effects to patch together.

Also: [Windows 32-bit](https://github.com/teamoth/timbient-modules/releases/download/latest/Timbient-Modules-Windows-32bit.zip) · [all downloads](https://github.com/teamoth/timbient-modules/releases/tag/latest)

## What it is

Well, it's sort of growing and developing as I figure out what I want from an ambient music making app, but where this is different from a DAW is that there isn't just one BPM, and you don't need to input specific notes. Everything _can_ be clock-synced together (and there _are_ note-entry modules), but the idea is that separate voices can have completely different timings from each other, and you choose a scale, key and range in which notes are generated.

Sessions can be blended!!! While one plays, the next loads in the background, then fades in as the first fades out, keeping the overall level steady. A blend can run over a set time or be moved by hand, by dragging or with a MIDI knob, which makes it easy to drift from one piece to the next in a live set.

The plan is to keep it as CPU light as possible, so that it'll run well on older hardware too, including 32-bit Windows machines.

Things that inspired timbient modules: SunVox, Monome Norns scripts, Polyend Tracker, Modular synths.
https://www.instagram.com/timbient/ 

## Installing

**Mac** (Intel and Apple Silicon, macOS 10.13 or later): unzip, drag *Timbient Modules.app* into Applications, then run this once in Terminal (the app isn't signed with an Apple developer certificate):

```
xattr -cr "/Applications/Timbient Modules.app"
```

**Windows**: unzip and run *Timbient Modules.exe*. If Windows warns, click *More info*, then *Run anyway*.

**Linux**: unpack and run *timbient-modules*.

Sessions saved from the app can also be played on a phone or tablet with the [web player](player/).
