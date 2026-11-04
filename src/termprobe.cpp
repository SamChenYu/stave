#include "termprobe.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <system_error>

const char* const kTerminalQueries =
    "\x1b[16t\x1b[14t\x1b]10;?\x1b\\\x1b]11;?\x1b\\\x1b]4;1;?\x1b\\\x1b]4;2;?\x1b\\\x1b]4;3;?\x1b\\"
    "\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[c";

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
    r.red = findColourReply(received, "\x1b]4;1;rgb:");
    r.green = findColourReply(received, "\x1b]4;2;rgb:");
    r.yellow = findColourReply(received, "\x1b]4;3;rgb:");
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
    t.red = r.red;
    t.green = r.green;
    t.yellow = r.yellow;
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

namespace {

// Diff inks that read well on a dark background, and on a light one: the usual green, yellow and
// red, then backups for when those clash with the text colour (blue, orange and magenta).
constexpr RGB kAddedOnDark{95, 200, 115}, kChangedOnDark{230, 190, 70}, kRemovedOnDark{240, 95, 95};
constexpr RGB kAddedOnLight{25, 130, 55}, kChangedOnLight{175, 110, 0}, kRemovedOnLight{195, 35, 40};
constexpr RGB kAddedBackupOnDark{90, 170, 255}, kChangedBackupOnDark{255, 150, 40}, kRemovedBackupOnDark{230, 110, 230};
constexpr RGB kAddedBackupOnLight{25, 95, 200}, kChangedBackupOnLight{200, 90, 0}, kRemovedBackupOnLight{165, 40, 160};

bool isDark(RGB c) { return 299 * c.r + 587 * c.g + 114 * c.b < 128 * 1000; }

double linear(uint8_t v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(RGB c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }

std::string hex(RGB c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

}  // namespace

double contrastRatio(RGB a, RGB b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

double markColourDistance(RGB a, RGB b) {
    // The a and b axes of OKLab (Björn Ottosson's): green to red, and blue to yellow.
    auto ab = [](RGB c) {
        const double r = linear(c.r), g = linear(c.g), bl = linear(c.b);
        const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * bl);
        const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * bl);
        const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * bl);
        return std::pair{1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                         0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
    };
    const auto [a1, b1] = ab(a);
    const auto [a2, b2] = ab(b);
    return std::hypot(a1 - a2, b1 - b2);
}

InkAndPaper chooseColours(const TerminalInfo& t, std::vector<std::string>* notes) {
    const bool known = t.fg && t.bg;
    InkAndPaper c{};
    c.ink = known ? *t.fg : RGB{0, 0, 0};
    c.paper = known ? *t.bg : RGB{255, 255, 255};
    c.opaque = !known;
    const bool dark = isDark(c.paper);

    struct Choice {
        Tone tone;
        const char* themeName;
        std::optional<RGB> theme;  // only used when the terminal's own colours are
        RGB usual, backup;
        RGB* out;
    };
    const Choice choices[] = {
        {Tone::Added, "green", t.green, dark ? kAddedOnDark : kAddedOnLight,
         dark ? kAddedBackupOnDark : kAddedBackupOnLight, &c.added},
        {Tone::Changed, "yellow", t.yellow, dark ? kChangedOnDark : kChangedOnLight,
         dark ? kChangedBackupOnDark : kChangedBackupOnLight, &c.changed},
        {Tone::Removed, "red", t.red, dark ? kRemovedOnDark : kRemovedOnLight,
         dark ? kRemovedBackupOnDark : kRemovedBackupOnLight, &c.removed},
    };
    const char* const toneNames[kToneCount] = {"text", "added", "changed", "removed"};

    std::vector<Tone> chosen;  // the marks picked so far, which later ones must differ from
    for (const Choice& ch : choices) {
        // Why a colour can't be used, or "" if it can.
        auto clash = [&](RGB colour) -> std::string {
            if (contrastRatio(colour, c.paper) < kMinMarkContrast) return "too faint on the background";
            if (markColourDistance(colour, c.ink) < kMinMarkDistance) return "too like the text colour";
            for (Tone other : chosen)
                if (markColourDistance(colour, c.inks()[size_t(other)]) < kMinMarkDistance)
                    return std::string("too like the ") + toneNames[int(other)] + " colour";
            return "";
        };
        struct Candidate {
            RGB colour;
            std::string name;
        };
        std::vector<Candidate> candidates;
        if (known && ch.theme) candidates.push_back({*ch.theme, std::string("the theme's ") + ch.themeName});
        candidates.push_back({ch.usual, "stave's usual " + std::string(ch.themeName)});
        candidates.push_back({ch.backup, "the backup"});

        std::string why;  // what was wrong with the ones passed over
        size_t pick = 0;
        for (; pick < candidates.size(); ++pick) {
            const std::string problem = clash(candidates[pick].colour);
            if (problem.empty()) break;
            why += std::string(why.empty() ? "" : ", and ") + candidates[pick].name + " " +
                   hex(candidates[pick].colour) + " is " + problem;
        }
        if (pick == candidates.size()) {  // nothing fits: the backup is the least likely to clash
            pick = candidates.size() - 1;
            why += ", but there's nothing better";
        }
        *ch.out = candidates[pick].colour;
        if (!why.empty()) {
            c.exact[size_t(ch.tone)] = true;
            if (notes) notes->push_back(std::string(toneNames[int(ch.tone)]) + " " + hex(*ch.out) + ": " + why);
        }
        chosen.push_back(ch.tone);
    }
    return c;
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
