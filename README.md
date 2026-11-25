# The Two Towers — PS Vita port

A PS Vita port of *The Lord of the Rings: The Two Towers* (Xbox, USA, 2002), built on
[The Two Towers Recompiled](https://github.com/Gumpolas/TooTawersRecomp) (static recompilation of the
Xbox game) and xboxrecomp. The Xbox code runs natively on the Vita's ARM CPU; the Xbox GPU commands are
translated to the Vita's GPU through vitaGL.

**No game data is included.** You need your own copy of the game (Xbox, USA disc image).

## Status

- Playable with sound, music and cutscenes (tested through every level up to Helm's Deep).
- Frame rate (500 MHz, 16:9): about 19–22 fps in most levels and in Helm's Deep, 15–18 fps in the busiest
  moments of the biggest battles. It is not 30 fps: the recompiled game code is the limit on the Vita's CPU.
- Overclocking to 500 MHz with PSVshell is recommended (about +12%).
- 16:9 widescreen by default (640x360 internal); 4:3 (544x408) is a little faster.
- Rain, weapon swing trails, far-away particles and the characters' round ground shadows are turned off
  for speed (see *Options* below to turn them back on).

## Requirements

- A PS Vita / PS TV with HENkaku/Ensō (any recent setup with VitaShell).
- About 4–5 GB free on ux0: for the game files.
- Your own copy of the game: *The Lord of the Rings: The Two Towers*, Xbox, **USA** (other regions are not supported).

## Install

1. Install `TwoTowers.vpk` with VitaShell.
2. Extract the **whole** Xbox disc image on a PC. Any XISO extractor works (for example
   `extract-xiso -x "The Two Towers (USA).iso"`, or xdvdfs).
3. Copy everything that was on the disc to the Vita at:

   ```
   ux0:data/twotowers/game_files/
   ```

   so that you have, for example, `ux0:data/twotowers/game_files/default.xbe` and
   `ux0:data/twotowers/game_files/FMV/`.
4. (Optional, smoother cutscenes) Convert the films to MP4 for the Vita's video decoder and put them in
   `ux0:data/twotowers/movies/`. Without them the original Bink films play in software.
   On a PC with ffmpeg, from the extracted `FMV` folder:

   ```
   for f in *.bik; do ffmpeg -i "$f" -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
     -vf scale=640:480 -b:v 2500k -c:a aac -b:a 160k -ar 48000 -ac 2 "${f%.bik}.mp4"; done
   ```
5. Start the game from the LiveArea bubble.

Saves, the shader cache and the log go to `ux0:data/twotowers/`. If the game crashes, please attach
`ux0:data/twotowers/twotowers_log.txt` and the crash dump from `ux0:data/` to your report.

## Options

Create `ux0:data/twotowers/env.txt` with one `NAME=VALUE` per line:

| Option | Effect |
|---|---|
| `TT_RAIN=1` | rain back on (slower in Helm's Deep) |
| `TT_TRAILS=1` | weapon swing trails back on |
| `TT_FARFX=1` | far-away particles back on |
| `TT_BLOBS=1` | characters' round ground shadows back on |
| `TT_WIDE=0` | 4:3 instead of the default 16:9 (a little faster) |
| `TT_XACTW=0` | game sound work back on the game thread: sound effects exactly in time, about 3% slower |
| `TT_PACE=1` | cap at 20 fps (15 when the game cannot keep up) |

## Building from source

The generated code is made from your own `default.xbe` and is never part of this repository.

1. vitasdk, plus SDL2 (SDL2 branch), vitaGL (`HAVE_GLSL_SUPPORT=1`), vitaShaRK, SceShaccCgExt, math-neon.
2. `python generate.py default.xbe`
3. `cmake -S twotowers -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake && ninja -C build`

This produces `TwoTowers.vpk`. Technical notes on the port are in `PORTING-ARM.md`; the PC version's README is `README-PC.md`.

## Credits

- The Two Towers Recompiled: Gumpolas and contributors -- https://github.com/Gumpolas/TooTawersRecomp
- The128ReBits, for their contributions to it (see `CONTRIBUTION_NOTICES.md`) -- https://github.com/The128ReBits
- xboxrecomp: sp00nz and contributors -- https://github.com/sp00nznet/xboxrecomp
- xemu, which parts of the runtime derive from (LGPL-2.1-or-later) -- https://github.com/xemu-project/xemu
- Volverine-Dude, co-op work on the PC version -- https://github.com/Volverine-Dude/TooTawersRecomp-coop
- vitaGL: Rinnegatamante -- https://github.com/Rinnegatamante/vitaGL
- PSVshell: Electry -- https://github.com/Electry/PSVshell
- vitasdk and the Vita homebrew community -- https://vitasdk.org
- PS Vita port: federicopellegrino994 (with technical assistance from Claude).

*The Lord of the Rings: The Two Towers* is © its respective owners. This project is not affiliated with
or endorsed by them.
