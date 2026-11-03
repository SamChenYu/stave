# stave

a music notation renderer on the CLI!

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
stave --svg FILE             # draw an SVG as wide as the terminal
stave --svg FILE --width 400 # ...or at another width (the height keeps the aspect ratio)
stave --test-pattern         # a test image, to check that sixel works in this terminal
stave --terminal-info        # what stave found out about this terminal
```

stave asks the terminal for its size in pixels and its text and background colours, and draws in
those colours on the terminal's own background, so images match the theme. Colours in the SVG
are ignored: what's drawn is how much ink covers each pixel. If the terminal doesn't answer (or
the output isn't a terminal), images are 800 pixels wide, in black on a white page.

## Tests

```
ctest --test-dir build -C Release --output-on-failure
```

## Dependencies

| Library | Version | Used for | Licence |
|---|---|---|---|
| [lunasvg](https://github.com/sammycage/lunasvg) | 3.5.0 | rendering SVG to pixels | MIT |
| [Verovio](https://github.com/rism-digital/verovio) | 6.4.0 | music engraving (not wired up yet) | LGPL-3.0 |
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | unit tests (test builds only) | MIT |

