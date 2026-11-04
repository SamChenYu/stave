#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "color.hpp"
#include "terminal.hpp"

// What we know about the terminal. Anything nobody told us is 0 or empty.
struct TerminalInfo {
    int cols = 0, rows = 0;
    int cellW = 0, cellH = 0;   // pixel size of one character cell
    std::optional<RGB> fg, bg;  // text and background colours
    // The colour theme's own red, green and yellow (ANSI colours 1, 2 and 3)
    std::optional<RGB> red, green, yellow;
    bool answered = false;      // the terminal replied at all
    bool sixel = false;         // it can show sixel images
    bool kitty = false;         // it can show kitty images

    int pixelWidth() const { return cols * cellW; }  // 0 if unknown
};

// The questions we ask the terminal, all sent at once:
//   CSI 16 t       cell size in pixels
//   CSI 14 t       text area size in pixels
//   OSC 10 ; ?     text colour
//   OSC 11 ; ?     background colour
//   OSC 4 ; n ; ?  the theme's red, green and yellow (colours 1, 2 and 3), for showing a diff
//   APC G ... a=q  whether it can show kitty images (a 1x1 test image it's asked not to show)
//   CSI c          device attributes, which every terminal answers; a 4 among them means sixel.
//                  Terminals answer in order, so this reply means all the others have arrived
//                  (or were never coming).
extern const char* const kTerminalQueries;

struct TerminalReplies {
    bool complete = false;         // the device-attributes reply has arrived
    int cellW = 0, cellH = 0;      // from CSI 16 t
    int windowW = 0, windowH = 0;  // from CSI 14 t
    std::optional<RGB> fg, bg;
    std::optional<RGB> red, green, yellow;
    bool sixel = false, kitty = false;
};

// Picks the answers out of whatever the terminal has sent so far, ignoring anything else.
TerminalReplies parseTerminalReplies(std::string_view received);

// Merges the OS's window size with the terminal's answers. The terminal's cell size is the
// most precise; failing that, a window size in pixels divided by the columns and rows. The cell
// is then returned in physical device pixels: on a HiDPI display the terminal reports points, so
// it is scaled up by the OS's device width over that point width, keeping images full width.
TerminalInfo combineTerminalInfo(const WindowSize&, const TerminalReplies&);

// Asks the OS and then the terminal, waiting at most timeoutMs for answers. Only asks the
// terminal when stdin and stdout are both a terminal. Construct a ConsoleSetup first.
TerminalInfo probeTerminal(int timeoutMs = 300);

struct InkAndPaper {
    RGB ink, paper;
    bool opaque;  // false: bare paper is left unpainted, so the terminal's background shows through
    // The inks for bars marked by --added, --changed and --removed
    RGB added, changed, removed;
    // Tones whose ink stands in for a theme colour that clashed (indexed by Tone). Braille, which
    // otherwise uses the terminal's own green, yellow and red, gives these as exact colours.
    std::array<bool, kToneCount> exact{};

    constexpr Inks inks() const { return {ink, added, changed, removed}; }
};

// With both of the terminal's colours known: ink in its text colour on a transparent background,
// so the image looks like part of the terminal. Otherwise black ink on a white page, which can be
// read on any theme.
//
// Marked bars are in the theme's own green (added), yellow (changed) and red (removed), unless a
// colour would be hard to pick out: too faint on the background (contrast under kMinMarkContrast),
// or too like the text colour or an earlier mark's colour (see markColourDistance). Then, and when
// the theme didn't say, stave's own colour for a dark or light background is used, and if that
// clashes too a backup (blue, orange or magenta). Each replacement is explained in `notes`, if given.
InkAndPaper chooseColours(const TerminalInfo&, std::vector<std::string>* notes = nullptr);

// The contrast a mark's colour needs against the background (WCAG's ratio, 1 to 21): 3 is the
// usual minimum for graphics and large shapes.
constexpr double kMinMarkContrast = 3;

// How different two colours look, ignoring how light they are: the distance between their hues
// and colourfulness in OKLab. Grey and black have no colour, so any real colour stands out from
// them; green on lime green doesn't. Marks need kMinMarkDistance from the text colour and each other.
double markColourDistance(RGB, RGB);
constexpr double kMinMarkDistance = 0.1;

// WCAG's contrast ratio between two colours, from 1 (the same) to 21 (black and white).
double contrastRatio(RGB, RGB);

// Near-black ink on a cream page, like printed music, whatever the terminal's colours.
constexpr InkAndPaper kPaperColours{{25, 25, 30}, {250, 247, 238}, true, {30, 125, 55}, {170, 105, 0}, {190, 35, 40}};

// How images are sent to the terminal.
enum class Output {
    Sixel,    // bands of six-pixel columns, in up to 256 colours; widely supported
    Kitty,    // raw pixels with transparency; kitty, Ghostty, WezTerm
    Braille,  // braille characters, 2x4 dots each, in the text colour; works almost anywhere
};
const char* outputName(Output);

// Sixel if the terminal can show it, otherwise braille. A terminal that didn't reply at all (or
// isn't a terminal, like a file) gets sixel. Kitty is disabled for now (it misbehaves under iTerm2),
// so it is never chosen automatically, though --kitty can still force it.
Output chooseOutput(const TerminalInfo&);
