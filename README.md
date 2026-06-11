![MiniPlusZoom](miniplus-zoom-logo.png)

# MiniPlusZoom

**A freeze-and-zoom inspection tool for the Miyoo Mini Plus (Onion OS).**

MiniPlusZoom pauses your game and hands you a magnifying glass. Press one button combo, the game freezes, and you get a clean 5x zoom you can pan around with the D-pad. Press it again and the game resumes exactly where it left off. No save states touched, no settings changed, nothing modified.

Built for looking more closely at delicious game art. 

## Controls

| Action | Buttons |
|---|---|
| Toggle zoom ON | MENU + L1 |
| Pan around | D-pad |
| Toggle zoom OFF | MENU + L1 |

## How it works

MiniPlusZoom runs as a tiny background daemon (~0% CPU while idle). When you trigger it, it:

1. Pauses the emulator process (SIGSTOP) so the screen stops redrawing
2. Snapshots the framebuffer (`/dev/fb0`)
3. Redraws the screen as a 5x nearest-neighbor zoom — crisp pixels, no blur
4. On exit, restores the original frame and resumes the emulator (SIGCONT)

The game itself never knows anything happened.

## Install

1. Download `zoom_daemon` and `launch_zoom.sh` from this repository
2. Plug your Onion OS SD card into your computer
3. Copy `zoom_daemon` into `.tmp_update/bin/`
4. Copy `launch_zoom.sh` into `.tmp_update/startup/`
5. Put the card back, boot up, launch a game, press MENU + L1

To uninstall, delete those two files. That's the whole footprint.

![Demo](miniplus-zoom-demo.gif)

## If you'd like to re-build from source

You need Docker installed. Put `zoom_daemon.c` in a folder, open a terminal in that folder, and run:

```
docker run --rm -v "%cd%:/work" -w /work ubuntu:22.04 bash -c "apt-get update -q && apt-get install -y -q gcc-arm-linux-gnueabihf && arm-linux-gnueabihf-gcc -O2 -static -o zoom_daemon zoom_daemon.c && echo BUILD SUCCESS"
```

(On Mac/Linux, replace `"%cd%:/work"` with `"$(pwd):/work"`.)

## Compatibility

Tested on the **Miyoo Mini Plus** running **Onion OS Stable: V4.3.1-1**. The button key codes are specific to the Mini Plus — the original Miyoo Mini uses different codes and would need `zoom_daemon.c` edited and rebuilt.

## The story

This is my first piece of software. I'm not a programmer. I'm an artist and graphic designer who wanted to look closer at pixel art on a handheld, was told that wasn't a thing, and decided it should a thing. Creating this took 3 days of trouble-shooting and vibe-coding with Claude Code, a cross-compiler, a button-code detector, one stubborn double-buffering bug, lots of trial and error. Ultimately super easy and worth it. 



## License

MIT — see [LICENSE](LICENSE). Use it, fork it, improve it.
