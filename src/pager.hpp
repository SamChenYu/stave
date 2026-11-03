#pragma once
#include <string>

#include "color.hpp"
#include "svgimage.hpp"
#include "termprobe.hpp"
#include "view.hpp"

// Where things go on a screen of cols x rows characters: the image on top, a status line on the
// bottom row. cellW and cellH are one character's size in pixels, or 0 if unknown; then a typical
// size is assumed and a spare row left, so a wrong guess can't push the screen up.
struct PagerLayout {
    int imageRows;       // text rows the image covers
    int imageW, imageH;  // image size in pixels; the height is a whole number of sixel bands
};
PagerLayout pagerLayout(int cols, int rows, int cellW, int cellH);

// The status line, cut or padded to exactly `width` characters: the name, where the view is
// ("top", "end", "all" or a percentage of the way down), the zoom, and the keys.
std::string statusLine(const std::string& name, const View&, int width);

struct PagerOptions {
    std::string name;   // shown in the status line
    RGB ink, paper;     // paper is painted, so each frame fully covers the one before
    int startWidth = 0;  // how wide to draw the image at first, in pixels; 0 fits the screen's width
};

// Shows the document full screen, on the terminal's alternate screen, until the user quits.
// Needs an interactive terminal; returns false (having changed nothing) if input can't be read.
bool runPager(const SvgDocument&, const TerminalInfo&, const PagerOptions&);
