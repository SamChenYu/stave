#pragma once
#include <string>
#include <string_view>

#include "support/image.hpp"

struct SixelDecodeResult {
    Image image;
    std::string error;  // empty on success
};

// Decodes one sixel image (ESC P ... q ... ESC \) into pixels, as a terminal would show it on a
// transparent background: pixels no colour was painted on stay transparent.
//
// It is deliberately stricter than a terminal, so that encoder bugs show up as errors rather than
// as slightly wrong pictures: the raster attributes must come before any pixel data and fix the
// size, every painted pixel must fall inside that size, colours must be defined (as RGB) before
// they are used, and any byte outside the sixel grammar is rejected.
SixelDecodeResult decodeSixel(std::string_view data);
