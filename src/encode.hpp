#pragma once
#include <string>

#include "canvas.hpp"
#include "color.hpp"

// Turns a coverage canvas into a sixel image, ready to write to the terminal.
// Each pixel is drawn as a blend of paper and ink, weighted by its coverage.
// With opaque = false, bare paper is left unpainted so the terminal's own background shows
// through; paper is then only used to blend soft edges (sixel has no partial transparency).
std::string encodeSixel(const Canvas&, RGB ink, RGB paper, bool opaque);
