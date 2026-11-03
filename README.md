# stave

a music notation renderer on the CLI!

## Build

Requires CMake 3.20+ and a C++20 compiler (MSVC 2022, GCC 11+ or Clang 14+).

```
git submodule update --init
cmake -S . -B build
cmake --build build --config Release
```

## Tests

```
ctest --test-dir build -C Release --output-on-failure
```

The tests are built by default (`-DSTAVE_BUILD_TESTS=OFF` skips them). Besides unit tests, golden
tests compare pixels: each one draws a known input, encodes it as sixel, decodes those bytes the way
a terminal would, and compares the result with an expected image in `tests/golden/`.

When a golden test fails, it saves `<name>.actual.png` and `<name>.diff.png` (differing pixels in
red) to `build/tests/output/`, and the failure message gives their paths. If the change is
intended, accept the new images and review them before committing:

```
STAVE_UPDATE_GOLDEN=1 ctest --test-dir build -C Release      # bash
$env:STAVE_UPDATE_GOLDEN=1; ctest --test-dir build -C Release # PowerShell (then remove the variable)
```

## Dependencies

| Library | Version | Used for | Licence |
|---|---|---|---|
| [lunasvg](https://github.com/sammycage/lunasvg) | 3.5.0 | rendering SVG to pixels | MIT |
| [Verovio](https://github.com/rism-digital/verovio) | 6.4.0 | music engraving (not wired up yet) | LGPL-3.0 |
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | unit tests (test builds only) | MIT |

