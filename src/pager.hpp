#pragma once
#include <string>
#include <vector>

#include "color.hpp"
#include "compare.hpp"
#include "encode.hpp"
#include "score.hpp"
#include "svgimage.hpp"
#include "termprobe.hpp"
#include "view.hpp"

// Where things go on a screen of cols x rows characters: the image on top, a status line on the
// bottom row. cellW and cellH are one character's size in pixels, or 0 if unknown; then a typical
// size is assumed and a spare row left, so a wrong guess can't push the screen up. For braille,
// the image's "pixels" are dots: 2 across and 4 down per character, whatever the cell size.
struct PagerLayout {
    int imageRows;       // text rows the image covers
    int imageW, imageH;  // image size in pixels; for sixel and kitty, a whole number of sixel bands
};
PagerLayout pagerLayout(int cols, int rows, int cellW, int cellH, Output output = Output::Sixel);

// The status line, cut or padded to exactly `width` characters: the name, where the view is, the
// zoom (100% being the size it opened at), and the keys. Where the view is, for a score: which bars
// and staves are on screen ("bars 5-9 of 120  staves 1-4 of 4"). Otherwise "left", "right" or "N%
// across", if the image is wider than the screen; "top", "bottom" or "N% down", if it's taller;
// "all" if neither.
std::string statusLine(const std::string& name, const View&, int width,
                       const std::vector<std::string>& barLabels = {});

// The starting zoom for a score, which is one long system: fitting its width would make it tiny.
// Instead each staff space is drawn spacePx pixels tall; and if the whole system then fits on
// screen with room to spare, it's enlarged to fill the height, up to twice that size.
// staffSpaceUnits is the image's units per staff space.
double scoreStartZoom(double docW, double docH, double staffSpaceUnits, const PagerLayout&, double spacePx);

// A comfortable staff space to start at: a staff (four spaces) about two and a half text rows tall,
// for sixel and kitty with cells cellH pixels tall (0 if unknown); four dots for braille, the least
// that keeps the staff lines apart.
double readableStaffSpace(Output, int cellH);

// The View's stops for a score: each bar's start and each staff's band. Moving to a bar leaves room
// to its left for the pinned column (see prepareForPaging) and 0.8 staff spaces, so its barline shows.
View::Stops scoreStops(const ScoreLayout&, double docHeight);

struct PagerOptions {
    std::string name;             // shown in the status line
    Output output = Output::Sixel;
    Inks inks = oneInk({0, 0, 0});  // plain ink, then the inks for marked bars
    ToneColours brailleColours = terminalToneColours();  // braille's colour for each tone
    RGB paper{255, 255, 255};     // sixel paints paper, so each frame fully covers the one before
    bool transparent = false;     // kitty: leave paper unpainted, showing the terminal's background
    int startWidth = 0;           // how wide to draw the image at first, in pixels (0: see below)
    double staffSpaceUnits = 0;   // for a score: units per staff space, to start at a readable size;
                                  // 0 (and no startWidth): start fitting the screen's width
    View::Stops stops;            // for a score: where its bars and staves are
    int startBar = 1, startStaff = 1;  // for a score: where to start, counting from 1
    std::vector<std::string> barLabels;  // for a score: each bar's printed number, for the status line
};

// The view the pager opens with, for an image area of layout's size: the starting zoom (from
// startWidth, or a readable staff size for a score, or fitting the width), and for a score its
// stops and starting bar and staff. cellH is the terminal's cell height in pixels (0 if unknown).
View startView(const SvgDocument&, const PagerOptions&, const PagerLayout&, int cellH);

// Shows the document full screen, on the terminal's alternate screen, until the user quits.
// Needs an interactive terminal; returns false (having changed nothing) if input can't be read.
bool runPager(const SvgDocument&, const TerminalInfo&, const PagerOptions&);

// ---- Two versions side by side (--compare): the old on the left, the new on the right.

struct CompareOptions {
    // Each version's name, stops, bar labels and starting place. How to draw (output, inks,
    // paper) comes from `after`, and so do the changes n and N go to, which should include the
    // old version's, moved onto the new one's bars.
    PagerOptions before, after;
    BarMap bars;  // which bar of one is which bar of the other
};

// The two halves of the screen. For braille, widths are in dots, two to a character.
struct CompareLayout {
    PagerLayout screen;   // the whole image area
    SplitWidths split;    // its two halves and the gap between them
    int leftCols;         // the left half's width in characters, for the status line
};
CompareLayout compareLayout(int cols, int rows, int cellW, int cellH, Output output = Output::Sixel);

struct CompareViews {
    View before, after;
};

// The views a comparison opens with: the new version where startView would put it, in the right
// half, and the old one at the same scale in the left half, at the matching bar.
CompareViews startCompareViews(const SvgDocument& before, const SvgDocument& after, const CompareOptions&,
                               const CompareLayout&, int cellH);

// Keeps the old version beside the new: moves `before` to the bar matching the first bar on the
// new version's screen, and to the same staff.
void followView(View& before, const View& after, const BarMap&);

// Applies a key to a comparison: the new version moves, and the old follows it. Zooming zooms both.
// Returns true if what's on screen changed.
bool applyCompareKey(CompareViews&, Key, const BarMap&);

// The status line for a comparison, cut or padded to `width` characters: the old version's name
// and bars under the left half, then the new version's, the zoom and the keys under the right.
std::string compareStatusLine(const CompareOptions&, const CompareViews&, int leftCols, int width);

// The whole screen of a comparison: both halves, drawn, side by side.
Canvas renderCompare(const SvgDocument& before, const SvgDocument& after, const CompareViews&,
                     const CompareLayout&);

// Shows two versions side by side, full screen, until the user quits. Like runPager, returns
// false if input can't be read.
bool runComparePager(const SvgDocument& before, const SvgDocument& after, const TerminalInfo&,
                     const CompareOptions&);
