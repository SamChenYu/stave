#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// A grayscale ink-coverage image: each pixel says how much of it is covered by ink, from
// 0 (bare paper) to 255 (solid ink). Colours are only chosen when the image is encoded.
struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // row by row, w * h bytes

    Canvas(int width, int height) : w(width), h(height), px(size_t(width) * size_t(height), 0) {}

    uint8_t& at(int x, int y) { return px[size_t(y) * w + x]; }
    uint8_t at(int x, int y) const { return px[size_t(y) * w + x]; }
};
