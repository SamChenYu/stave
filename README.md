# stave

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Build: CMake](https://img.shields.io/badge/build-CMake-064F8C.svg?logo=cmake&logoColor=white)](https://cmake.org/)
[![Requires: Git](https://img.shields.io/badge/requires-Git-F05032.svg?logo=git&logoColor=white)](https://git-scm.com/)

### **a music notation renderer on the CLI!**

*ascii and unicode is too coarse to render good musical notation, therefore it gets rendered to an svg using `Verovio` before getting rasterized with `lunasvg` onto the terminal via sixel*

## Build

Requires CMake 3.20+ and a C++20 compiler (MSVC 2022, GCC 11+ or Clang 14+).

```
git submodule update --init
cmake -S . -B build
cmake --build build --config Release
```

## Run

Needs a terminal that can show sixel graphics, such as Windows Terminal 1.22+, WezTerm, foot or
iTerm2.

```
stave score.musicxml         # show a score full screen (MusicXML .musicxml/.xml/.mxl, MEI or ABC)
stave drawing.svg            # ...or an SVG
stave FILE --width 400       # start at another width
stave FILE --print           # print it inline instead, like cat
stave --test-pattern         # a test image, to check that sixel works in this terminal
stave --terminal-info        # what stave found out about this terminal
```

In a terminal, a file opens in a full-screen viewer; when the output isn't a terminal, it prints
instead. A score is laid out as one long line of music that scrolls sideways, starting with each
staff about two and a half text rows tall. stave finds Verovio's music fonts in `verovio-data/`
next to the executable (the build puts them there); `--verovio-data DIR` points elsewhere.

| Key | Action |
|---|---|
| ← → ↑ ↓ / h l k j | move a tenth of the screen |
| Shift+← → / H L | one screen sideways |
| PgUp PgDn / Space | one screen up/down |
| Home End / g G | start / end |
| + - | zoom |
| q / Esc / Ctrl+C | quit |

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

