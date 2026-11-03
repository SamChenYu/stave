#include "termprobe.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <string>
#include <system_error>

const char* const kTerminalQueries =
    "\x1b[16t\x1b[14t\x1b]10;?\x1b\\\x1b]11;?\x1b\\\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[c";

namespace {

// Reads a decimal number at s[pos], moving pos past it.
bool readInt(std::string_view s, size_t& pos, int& out) {
    const auto [end, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), out);
    if (ec != std::errc() || end == s.data() + pos) return false;
    pos = size_t(end - s.data());
    return true;
}

// Finds a reply of the form <prefix> H ; W t, as in ESC [ 6 ; 19 ; 9 t.
bool findSizeReply(std::string_view s, std::string_view prefix, int& w, int& h) {
    for (size_t at = s.find(prefix); at != std::string_view::npos; at = s.find(prefix, at + 1)) {
        size_t pos = at + prefix.size();
        int hh = 0, ww = 0;
        if (readInt(s, pos, hh) && pos < s.size() && s[pos++] == ';' && readInt(s, pos, ww) && pos < s.size() &&
            s[pos] == 't' && hh > 0 && ww > 0) {
            w = ww;
            h = hh;
            return true;
        }
    }
    return false;
}

// Finds a colour reply such as ESC ] 11 ; rgb:1e1e/1e1e/2e2e. Each component has 1 to 4 hex
// digits, scaled to 0-255 (so "f", "ff" and "ffff" all mean 255).
std::optional<RGB> findColourReply(std::string_view s, std::string_view prefix) {
    const size_t at = s.find(prefix);
    if (at == std::string_view::npos) return std::nullopt;
    size_t pos = at + prefix.size();
    uint8_t v[3];
    for (int i = 0; i < 3; ++i) {
        unsigned x = 0;
        const auto [end, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), x, 16);
        const size_t digits = size_t(end - (s.data() + pos));
        if (ec != std::errc() || digits == 0 || digits > 4) return std::nullopt;
        const unsigned max = (1u << (4 * digits)) - 1;
        v[i] = uint8_t((x * 255 + max / 2) / max);
        pos += digits;
        if (i < 2 && (pos >= s.size() || s[pos++] != '/')) return std::nullopt;
    }
    return RGB{v[0], v[1], v[2]};
}

// The device-attributes reply, ESC [ ? digits and semicolons c: its numbers, or nothing.
std::optional<std::string_view> deviceAttributes(std::string_view s) {
    for (size_t at = s.find("\x1b[?"); at != std::string_view::npos; at = s.find("\x1b[?", at + 1)) {
        const size_t end = s.find_first_not_of("0123456789;", at + 3);
        if (end != std::string_view::npos && s[end] == 'c') return s.substr(at + 3, end - at - 3);
    }
    return std::nullopt;
}

bool hasAttribute(std::string_view attributes, std::string_view wanted) {
    for (size_t start = 0; start <= attributes.size();) {
        const size_t end = std::min(attributes.find(';', start), attributes.size());
        if (attributes.substr(start, end - start) == wanted) return true;
        start = end + 1;
    }
    return false;
}

}  // namespace

TerminalReplies parseTerminalReplies(std::string_view received) {
    TerminalReplies r;
    const std::optional<std::string_view> attributes = deviceAttributes(received);
    r.complete = attributes.has_value();
    r.sixel = attributes && hasAttribute(*attributes, "4");
    r.kitty = received.find("\x1b_Gi=31;OK") != std::string_view::npos;
    findSizeReply(received, "\x1b[6;", r.cellW, r.cellH);
    findSizeReply(received, "\x1b[4;", r.windowW, r.windowH);
    r.fg = findColourReply(received, "\x1b]10;rgb:");
    r.bg = findColourReply(received, "\x1b]11;rgb:");
    return r;
}

TerminalInfo combineTerminalInfo(const WindowSize& ws, const TerminalReplies& r) {
    TerminalInfo t;
    t.cols = ws.cols;
    t.rows = ws.rows;

    // Cell size in logical points, as the terminal reports it: CSI 16 t directly, or else a
    // text-area size (CSI 14 t, failing that the OS's pixel size) split over the rows and columns.
    // pointW keeps that text-area width in the same points, to spot a HiDPI display just below.
    int cellW = 0, cellH = 0, pointW = 0;
    if (r.cellW > 0 && r.cellH > 0) {
        cellW = r.cellW;
        cellH = r.cellH;
        if (t.cols > 0) pointW = r.cellW * t.cols;
    } else if (t.cols > 0 && t.rows > 0) {
        const int w = r.windowW > 0 ? r.windowW : ws.pixelW;
        const int h = r.windowW > 0 ? r.windowH : ws.pixelH;
        if (w > 0 && h > 0) {
            cellW = w / t.cols;
            cellH = h / t.rows;
            pointW = w;
        }
    }

    // Image protocols (sixel, kitty) draw one image pixel per physical device pixel, but on a HiDPI
    // display terminals like iTerm2 answer the size queries above in logical points — half the
    // device pixels. The OS's TIOCGWINSZ reports the true device width, so when it is a whole
    // multiple of the point width, that multiple is the display's scale factor, and growing the
    // cell by it makes a full-width image really span the window. Without the OS size the scale is
    // taken as 1 (an image may then come out half width on HiDPI, the best guess left).
    int scale = 1;
    if (ws.pixelW > 0 && pointW > 0) {
        const int s = (ws.pixelW + pointW / 2) / pointW;  // round(ws.pixelW / pointW)
        if (s > 1) scale = s;
    }
    t.cellW = cellW * scale;
    t.cellH = cellH * scale;

    t.answered = r.complete;
    t.sixel = r.sixel;
    t.kitty = r.kitty;
    t.fg = r.fg;
    t.bg = r.bg;
    return t;
}

TerminalInfo probeTerminal(int timeoutMs) {
    const WindowSize ws = queryWindowSize();
    TerminalReplies replies;
    if (isInteractive()) {
        RawInput in;
        if (in.ok() && writeAll(kTerminalQueries)) {
            using Clock = std::chrono::steady_clock;
            const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
            std::string received;
            while (!(replies = parseTerminalReplies(received)).complete) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                if (left <= 0 || !in.read(received, int(left))) break;
            }
        }
    }
    return combineTerminalInfo(ws, replies);
}

InkAndPaper chooseColours(const TerminalInfo& t) {
    if (t.fg && t.bg) return {*t.fg, *t.bg, false};
    return {{0, 0, 0}, {255, 255, 255}, true};
}

const char* outputName(Output o) {
    switch (o) {
        case Output::Sixel: return "sixel";
        case Output::Kitty: return "kitty";
        case Output::Braille: return "braille";
    }
    return "";
}

Output chooseOutput(const TerminalInfo& t) {
    // Kitty is disabled for now: iTerm2's kitty support misbehaves, so we never pick it
    // automatically. The implementation is kept and can still be forced with --kitty.
    if (t.sixel || !t.answered) return Output::Sixel;
    return Output::Braille;
}
