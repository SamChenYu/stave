# stave

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Build: CMake](https://img.shields.io/badge/build-CMake-064F8C.svg?logo=cmake&logoColor=white)](https://cmake.org/)
[![Requires: Git](https://img.shields.io/badge/requires-Git-F05032.svg?logo=git&logoColor=white)](https://git-scm.com/)

### **a music notation renderer on the CLI!**

![Render](media/render.png)

*ascii and unicode is too coarse to render good musical notation, therefore it gets rendered to an svg using `Verovio` before getting rasterized with `lunasvg` onto the terminal via sixel. kitty is supported, but buggy so disabled for now. if your terminal has niether of these, braille is used as a fallback.*

## Build

Requires CMake 3.20+ and a C++20 compiler (MSVC 2022, GCC 11+ or Clang 14+).

```
git submodule update --init
cmake -S . -B build
cmake --build build --config Release
```

## Run

Draws real pixels in terminals that can show images: kitty graphics (kitty, Ghostty, WezTerm) or
sixel (Windows Terminal 1.22+, WezTerm, foot, iTerm2, ...). Anywhere else it falls back to braille
characters, 2x4 dots each.

```
stave examples/beethoven-op18no1-mvt1.mxl   # a score full screen (MusicXML .musicxml/.xml/.mxl, MEI or ABC)
stave drawing.svg            # ...or an SVG
stave FILE --bar 9 --staff 2 # start somewhere else in a score (counting from 1)
stave FILE --width 400       # start at another width
stave FILE --paper           # dark ink on a cream page, instead of the terminal's own colours
stave FILE --sixel           # force an output: --sixel, --kitty or --braille
stave FILE --print           # print it inline instead, like cat
stave FILE --png out.png     # save the first screen as a 1600x900 PNG
stave FILE --bench           # time loading, drawing and each output
stave --test-pattern         # a test image, to check that this terminal can show images
stave --terminal-info        # what stave found out about this terminal
```

In a terminal, a file opens in a full-screen viewer; when the output isn't a terminal, it prints
instead. A score is laid out as one long line of music that scrolls sideways, starting with each
staff about two and a half text rows tall, and the opening clefs, key signatures and part names
stay pinned on the left. stave draws in the terminal's own colours, and finds Verovio's music
fonts in `verovio-data/` next to the executable (the build puts them there); `--verovio-data DIR`
points elsewhere. Set `STAVE_DUMP_SVG=file.svg` to save the SVG Verovio drew, for debugging.

| Key | In a score | In an SVG |
|---|---|---|
| ← → / h l | one bar | a tenth of the screen |
| ↑ ↓ / k j | one staff | a tenth of the screen |
| Shift+← → / H L | one screen sideways | one screen sideways |
| PgUp PgDn / Space | one screen up/down | one screen up/down |
| Home End / g G | first / last bar | start / end |
| + - | zoom | zoom |
| q / Esc / Ctrl+C | quit | quit |

The example scores in `examples/` are public-domain pieces; see `examples/README.md`.

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

