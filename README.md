# stave

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Build: CMake](https://img.shields.io/badge/build-CMake-064F8C.svg?logo=cmake&logoColor=white)](https://cmake.org/)
[![Requires: Git](https://img.shields.io/badge/requires-Git-F05032.svg?logo=git&logoColor=white)](https://git-scm.com/)

### **a music notation renderer on the CLI!**

![Render](media/render.png)

*ascii and unicode is too coarse to render good musical notation, therefore it gets rendered to an svg using `Verovio` before getting rasterized with `lunasvg` onto the terminal via sixel. kitty is supported, but buggy so disabled for now. if your terminal has niether of these, braille is used as a fallback.*

It can also colour bars green, yellow and red to show what changed between two versions of a score,
which is how [motif](https://github.com/SamChenYu/motif) shows its diffs.

## Build

Requires CMake 3.20+ and a C++20 compiler (MSVC 2022, GCC 11+ or Clang 14+).

```
git submodule update --init
cmake -S . -B build
cmake --build build --config Release
```

## Run

Draws  pixels in terminals that can show sixel images (Windows Terminal 1.22+, WezTerm, foot,
iTerm2). Anywhere else it falls back to braille characters, 2x4 dots each (looks pretty bad tho). Kitty graphics
(kitty, Ghostty, WezTerm) also work but are never picked automatically for now, as it flickers. using`--kitty` forces them.

```
stave examples/beethoven-op18no1-mvt1.mxl   # a score full screen (MusicXML .musicxml/.xml/.mxl, MEI or ABC)
stave drawing.svg            # ...or an SVG
stave FILE --bar 9 --staff 2 # start somewhere else in a score (bars by their printed number)
stave FILE --added 40-60 --changed 12-15@2 --removed 7  # colour bars to show a diff
stave FILE --marks diff.json # colour single notes too, from a file (see below)
stave FILE --name "song @ HEAD~1"  # what the status line calls it
stave FILE --width 400       # start at another width
stave FILE --paper           # dark ink on a cream page, instead of the terminal's own colours
stave FILE --sixel           # force an output: --sixel, --kitty or --braille
stave FILE --print           # print it inline instead, like cat
stave FILE --png out.png     # save the first screen as a 1600x900 PNG
stave FILE --bench           # time loading, drawing and each output
stave --test-pattern         # a test image, to check that this terminal can show images
stave --terminal-info        # what stave found out about this terminal
```



## Controls for the terminal pager

| Key | In a score | In an SVG |
|---|---|---|
| ← → / h l | one bar | a tenth of the screen |
| ↑ ↓ / k j | one staff | a tenth of the screen |
| Shift+← → / H L | one screen sideways | one screen sideways |
| PgUp PgDn / Space | one screen up/down | one screen up/down |
| Home End / g G | first / last bar | start / end |
| + - | zoom | zoom |
| n N | next / previous change | |
| q / Esc / Ctrl+C | quit | quit |

The example scores in `examples/` are public-domain pieces; see `examples/README.md`.

## Showing a diff

`--added`, `--changed` and `--removed` colour bars green, yellow and red. this is how [motif](https://github.com/SamChenYu/motif) shows what changed between two versions of a score.

```
stave new.musicxml --added 40-60 --changed 12-15@2,20@3 --removed 7
```

| Value | Marks |
|---|---|
| `7` | bar 7 |
| `12-15` | bars 12 to 15 |
| `12-15@2` | bars 12 to 15, only on staff 2 |
| `3,7-9@1,20` | any mix of those |

bars go by their printed numbers, so a pickup is usually bar 0. staves count from the top, starting at 1.

### single notes

a diff usually changes a few notes rather than whole bars, so `--marks` takes a JSON file that can pick out single notes:

```json
[
  {"tone": "changed", "bar": 12, "staff": 2, "at": "3/2", "pitch": 66},
  {"tone": "added", "bar": 14, "to": 16}
]
```

| Field | Means |
|---|---|
| `tone` | `added`, `changed` or `removed` |
| `bar` | the bar's printed number |
| `to` | the last bar, to mark a run of whole bars |
| `staff` | only this staff |
| `at` | only the notes starting here, in quarter notes from the barline: `2`, `1.5` or `"1/3"` |
| `pitch` | only the notes sounding this MIDI pitch (transposing instruments included) |
| `voice` | only the notes in this voice |
| `fill` | `true`: tint the bars' background in the tone, rather than colour their ink, like a diff's line highlight. notes marked in the same bars still show their colour on it |

with `at`, `pitch` or `voice` it marks notes, otherwise whole bars. if every note of a chord is marked the same, its stem and dots get coloured too. a mark that matches no note, or a note marked two different ways, is an error. `n` / `N` jump between changes.

### side by side

`--compare` shows two versions next to each other, the old on the left and the new on the right.
Keys move the new one and the old one follows, to the same bar and staff.

```
stave --compare old.musicxml new.musicxml --before-marks old.json --marks new.json --pairs 1-17=1-17,19-40=18-39
```

`--marks` (and `--added` and the like) mark the new version and `--before-marks` the old, so removed notes
can be shown in red where they were. `--pairs` says which bars of the old version are which of the new, as
runs of `OLD=NEW` bars: above, old bar 18 was deleted. Without it, bars are matched in order.
`--name` and `--before-name` name each side on the status line, `--bar` and `--staff` start the new version
somewhere else, and `n` / `N` jump between the changes on either side.

## Tests

```
ctest --test-dir build -C Release --output-on-failure
```

## Dependencies

| Library | Version | Used for | 
|---|---|---|
| [lunasvg](https://github.com/sammycage/lunasvg) | 3.5.0 | rendering SVG to pixels |
| [Verovio](https://github.com/rism-digital/verovio) | 6.4.0 | music notation rendering |
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | unit tests |

