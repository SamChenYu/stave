#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Pixel {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Pixel&) const = default;
};

// An RGBA image for tests, not premultiplied. Alpha 0 means nothing is drawn there, and such pixels
// always hold (0, 0, 0, 0), so two images are equal exactly when they look the same.
struct Image {
    int w = 0, h = 0;
    std::vector<Pixel> px;  // row by row

    Image() = default;
    Image(int width, int height) : w(width), h(height), px(size_t(width) * size_t(height)) {}

    Pixel& at(int x, int y) { return px[size_t(y) * w + x]; }
    const Pixel& at(int x, int y) const { return px[size_t(y) * w + x]; }
};

std::optional<Image> loadPng(const std::string& path);
bool savePng(const Image&, const std::string& path);

struct ImageDiff {
    bool sameSize = true;
    int differing = 0;  // pixels where some channel differs by more than the tolerance
    int maxDelta = 0;   // the largest channel difference anywhere
    Image visual;       // the expected image faded out, with differing pixels in red
};

// tolerance is the largest per-channel difference that still counts as equal.
ImageDiff compareImages(const Image& actual, const Image& expected, int tolerance = 0);
