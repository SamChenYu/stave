#pragma once
#include <optional>
#include <string>
#include <string_view>

#include "canvas.hpp"

// The largest image we'll render, in each direction: far beyond any terminal, small enough
// that a mistyped width can't ask for gigabytes.
constexpr int kMaxImageSide = 16384;

struct SvgRenderResult {
    std::optional<Canvas> canvas;  // set on success
    std::string error;             // set on failure
};

// Renders an SVG `width` pixels wide, with the height following its aspect ratio, as ink
// coverage: how opaque each pixel is. Colours are ignored, so a red shape and a black one
// come out the same; the ink colour is chosen when the canvas is encoded.
SvgRenderResult renderSvgFile(const std::string& path, int width);
SvgRenderResult renderSvgData(std::string_view svg, int width);
