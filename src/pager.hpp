#pragma once
#include <string>

#include "color.hpp"
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
std::string statusLine(const std::string& name, const View&, int width);

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
    RGB ink, paper;               // sixel paints paper, so each frame fully covers the one before
    bool transparent = false;     // kitty: leave paper unpainted, showing the terminal's background
    int startWidth = 0;           // how wide to draw the image at first, in pixels (0: see below)
    double staffSpaceUnits = 0;   // for a score: units per staff space, to start at a readable size;
                                  // 0 (and no startWidth): start fitting the screen's width
    View::Stops stops;            // for a score: where its bars and staves are
    int startBar = 1, startStaff = 1;  // for a score: where to start, counting from 1
};

// The view the pager opens with, for an image area of layout's size: the starting zoom (from
// startWidth, or a readable staff size for a score, or fitting the width), and for a score its
// stops and starting bar and staff. cellH is the terminal's cell height in pixels (0 if unknown).
View startView(const SvgDocument&, const PagerOptions&, const PagerLayout&, int cellH);

// Shows the document full screen, on the terminal's alternate screen, until the user quits.
// Needs an interactive terminal; returns false (having changed nothing) if input can't be read.
bool runPager(const SvgDocument&, const TerminalInfo&, const PagerOptions&);
