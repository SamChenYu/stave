# stave

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
stave --svg FILE             # show an SVG full screen, as wide as the terminal
stave --svg FILE --width 400 # ...starting at another width
stave --svg FILE --print     # print it inline instead, like cat
stave --test-pattern         # a test image, to check that sixel works in this terminal
stave --terminal-info        # what stave found out about this terminal
```

In a terminal, `--svg` opens a full-screen viewer; when the output isn't a terminal, it prints
instead.

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

| Library | Version | Used for | Licence |
|---|---|---|---|
| [lunasvg](https://github.com/sammycage/lunasvg) | 3.5.0 | rendering SVG to pixels | MIT |
| [Verovio](https://github.com/rism-digital/verovio) | 6.4.0 | music engraving (linked statically for now) | LGPL-3.0 |
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | unit tests (test builds only) | MIT |

