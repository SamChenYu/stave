#pragma once
#include <string>
#include <vector>

#include "canvas.hpp"
#include "color.hpp"

// Turns a coverage canvas into a sixel image, ready to write to the terminal.
// Each pixel is drawn as a blend of paper and ink, weighted by its coverage.
// With opaque = false, bare paper is left unpainted so the terminal's own background shows
// through; paper is then only used to blend soft edges (sixel has no partial transparency).
std::string encodeSixel(const Canvas&, RGB ink, RGB paper, bool opaque);

// Turns a coverage canvas into a kitty graphics image: raw pixels, sent base64-encoded in chunks
// of at most 4096 bytes. With opaque = false, each pixel is ink with alpha = coverage, so the
// terminal blends soft edges with whatever is behind them and paper isn't used. With moveCursor,
// the cursor ends up below the image (for printing); without it, it stays put (for the pager).
std::string encodeKitty(const Canvas&, RGB ink, RGB paper, bool opaque, bool moveCursor);

// Turns a coverage canvas into rows of braille characters (UTF-8), each covering 2x4 pixels. A dot
// is raised where coverage is at least kBrailleThreshold: at the sizes braille can show, staff lines
// are thinner than a dot (a quarter covered, at 4 dots per staff space), and must still show. The
// dots are drawn in the terminal's own text colour.
constexpr int kBrailleThreshold = 40;
std::vector<std::string> encodeBraille(const Canvas&);
