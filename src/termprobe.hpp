#pragma once
#include <optional>
#include <string_view>

#include "color.hpp"
#include "terminal.hpp"

// What we know about the terminal. Anything nobody told us is 0 or empty.
struct TerminalInfo {
    int cols = 0, rows = 0;
    int cellW = 0, cellH = 0;   // pixel size of one character cell
    std::optional<RGB> fg, bg;  // text and background colours
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
};

// With both of the terminal's colours known: ink in its text colour on a transparent background,
// so the image looks like part of the terminal. Otherwise black ink on a white page, which can
// be read on any theme.
InkAndPaper chooseColours(const TerminalInfo&);

// Near-black ink on a cream page, like printed music, whatever the terminal's colours.
constexpr InkAndPaper kPaperColours{{25, 25, 30}, {250, 247, 238}, true};

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
