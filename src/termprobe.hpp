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

    int pixelWidth() const { return cols * cellW; }  // 0 if unknown
};

// The questions we ask the terminal, all sent at once:
//   CSI 16 t       cell size in pixels
//   CSI 14 t       text area size in pixels
//   OSC 10 ; ?     text colour
//   OSC 11 ; ?     background colour
//   CSI c          device attributes, which every terminal answers. Terminals answer in order,
//                  so its reply means all the others have arrived (or were never coming).
extern const char* const kTerminalQueries;

struct TerminalReplies {
    bool complete = false;         // the device-attributes reply has arrived
    int cellW = 0, cellH = 0;      // from CSI 16 t
    int windowW = 0, windowH = 0;  // from CSI 14 t
    std::optional<RGB> fg, bg;
};

// Picks the answers out of whatever the terminal has sent so far, ignoring anything else.
TerminalReplies parseTerminalReplies(std::string_view received);

// Merges the OS's window size with the terminal's answers. The terminal's cell size is the
// most precise; failing that, a window size in pixels divided by the columns and rows.
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
