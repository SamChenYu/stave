#pragma once
#include <string>
#include <vector>

#include "canvas.hpp"
#include "color.hpp"

// Each encoder draws a pixel in the ink of its tone (see Canvas::tone), from `inks`. The versions
// taking one RGB ink draw every tone in it.
//
// A pixel with a fill (Canvas::fill) has its paper tinted: kFillCoverage/255 of the way from paper
// to that tone's ink, faint enough for ink in the same tone to stand out on it.
constexpr int kFillCoverage = 56;
RGB fillPaper(const Inks& inks, RGB paper, Tone fill);

// Turns a coverage canvas into a sixel image, ready to write to the terminal.
// Each pixel is drawn as a blend of paper and ink, weighted by its coverage.
// With opaque = false, bare paper is left unpainted so the terminal's own background shows
// through; paper is then only used to blend soft edges (sixel has no partial transparency).
std::string encodeSixel(const Canvas&, const Inks& inks, RGB paper, bool opaque);
inline std::string encodeSixel(const Canvas& c, RGB ink, RGB paper, bool opaque) {
    return encodeSixel(c, oneInk(ink), paper, opaque);
}

// Turns a coverage canvas into a kitty graphics image: raw pixels, sent base64-encoded in chunks
// of at most 4096 bytes. With opaque = false, each pixel is ink with alpha = coverage, so the
// terminal blends soft edges with whatever is behind them and paper isn't used. With moveCursor,
// the cursor ends up below the image (for printing); without it, it stays put (for the pager).
std::string encodeKitty(const Canvas&, const Inks& inks, RGB paper, bool opaque, bool moveCursor);
inline std::string encodeKitty(const Canvas& c, RGB ink, RGB paper, bool opaque, bool moveCursor) {
    return encodeKitty(c, oneInk(ink), paper, opaque, moveCursor);
}

// Turns a coverage canvas into rows of braille characters (UTF-8), each covering 2x4 pixels. A dot
// is raised where coverage is at least kBrailleThreshold: at the sizes braille can show, staff lines
// are thinner than a dot (a quarter covered, at 4 dots per staff space), and must still show. The
// dots are drawn in the terminal's own text colour; a character whose dots are mostly marked is in
// its tone's colour, from `colours`.
constexpr int kBrailleThreshold = 40;
using ToneColours = std::array<std::string, kToneCount>;  // the escape code that starts each tone's colour
std::vector<std::string> encodeBraille(const Canvas&, const ToneColours& colours);
std::vector<std::string> encodeBraille(const Canvas&);  // in terminalToneColours()
// The same, with a background behind each character whose pixels are mostly filled: the escape code
// that starts each fill's background, from `fills` (fillColours). Empty codes leave it alone.
std::vector<std::string> encodeBraille(const Canvas&, const ToneColours& colours, const ToneColours& fills);

// Backgrounds for braille's filled characters: fillPaper's colours as 24-bit codes, with the
// terminal's own background (reset) for plain.
ToneColours fillColours(const Inks& inks, RGB paper);

// The terminal's own text colour, green (added), yellow (changed) and red (removed), whatever its
// theme makes them.
ToneColours terminalToneColours();

// The same, except that the tones in `exact` are given as the exact colours in `inks` (24-bit
// colour codes, which nearly every terminal understands).
ToneColours toneColours(const Inks& inks, const std::array<bool, kToneCount>& exact);

// Saves a coverage canvas as a PNG, each pixel blended from paper to ink. False if it can't.
bool writePng(const Canvas&, const Inks& inks, RGB paper, const std::string& path);
