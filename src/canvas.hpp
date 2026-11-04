#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "color.hpp"

// A grayscale ink-coverage image: each pixel says how much of it is covered by ink, from
// 0 (bare paper) to 255 (solid ink). Colours are only chosen when the image is encoded: each pixel
// is in the ink of its tone (plain, unless it's marked as part of a diff).
struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> px;    // row by row, w * h bytes
    std::vector<uint8_t> tone;  // each pixel's Tone, row by row like px; empty when all are plain

    Canvas(int width, int height) : w(width), h(height), px(size_t(width) * size_t(height), 0) {}

    uint8_t& at(int x, int y) { return px[size_t(y) * w + x]; }
    uint8_t at(int x, int y) const { return px[size_t(y) * w + x]; }
    Tone toneAt(int x, int y) const { return tone.empty() ? Tone::Plain : Tone(tone[size_t(y) * w + x]); }
};
