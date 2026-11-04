#include "pager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "encode.hpp"
#include "keys.hpp"
#include "terminal.hpp"

namespace {

constexpr int kGuessCellW = 10, kGuessCellH = 20;  // a typical cell size, when the terminal won't say
constexpr int kPollMs = 100;                       // how often to check for a resized window

std::string moveTo(int row, int col) { return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H"; }

// Kitty keeps images until they're deleted: this deletes all of ours, without a reply.
const char* const kDeleteKittyImages = "\x1b_Ga=d,d=A,q=2\x1b\\";

// Switches to the alternate screen with the cursor hidden, and back again however the pager ends.
class FullScreen {
public:
    explicit FullScreen(Output output) : output_(output) { writeAll("\x1b[?1049h\x1b[?25l"); }
    ~FullScreen() {
        if (output_ == Output::Kitty) writeAll(kDeleteKittyImages);
        writeAll("\x1b[?25h\x1b[?1049l");
    }
    FullScreen(const FullScreen&) = delete;
    FullScreen& operator=(const FullScreen&) = delete;

private:
    Output output_;
};

void drawFrame(const SvgDocument& doc, const View& v, const PagerLayout& layout, int cols, int rows,
               const PagerOptions& opt, bool clear) {
    const Canvas c = doc.render(v.scale(), v.x(), v.y(), layout.imageW, layout.imageH);
    std::string f = "\x1b[?2026h";  // synchronized update: the terminal shows the whole frame at once
    if (clear) f += "\x1b[2J";
    switch (opt.output) {
        case Output::Sixel: f += moveTo(1, 1) + encodeSixel(c, opt.inks, opt.paper, /*opaque=*/true); break;
        case Output::Kitty:
            f += kDeleteKittyImages + moveTo(1, 1) +
                 encodeKitty(c, opt.inks, opt.paper, /*opaque=*/!opt.transparent, /*moveCursor=*/false);
            break;
        case Output::Braille: {
            const std::vector<std::string> lines = encodeBraille(c, opt.brailleColours);
            for (size_t r = 0; r < lines.size() && int(r) < layout.imageRows; ++r)
                f += moveTo(int(r) + 1, 1) + lines[r];
            break;
        }
    }
    // Reverse video, so the status line follows the terminal's own colours. It stops one column
    // short of the edge: writing the last cell of the last row can scroll the screen.
    f += moveTo(rows, 1) + "\x1b[2K\x1b[7m" + statusLine(opt.name, v, cols - 1) + "\x1b[0m";
    f += "\x1b[?2026l";
    writeAll(f);
}

}  // namespace

PagerLayout pagerLayout(int cols, int rows, int cellW, int cellH, Output output) {
    const bool known = cellW > 0 && cellH > 0;
    const int cw = known ? cellW : kGuessCellW, ch = known ? cellH : kGuessCellH;
    PagerLayout l{};
    l.imageRows = std::max(1, rows - 1);
    if (output == Output::Braille) {
        l.imageW = std::clamp(std::max(1, cols) * 2, 1, kMaxImageSide);
        l.imageH = std::clamp(l.imageRows * 4, 4, kMaxImageSide);
        return l;
    }
    const int rowsForImage = known ? l.imageRows : std::max(1, l.imageRows - 1);
    l.imageW = std::clamp(std::max(1, cols) * cw, 1, kMaxImageSide);
    l.imageH = std::clamp(rowsForImage * ch / 6 * 6, 6, kMaxImageSide / 6 * 6);  // sixel draws 6-pixel bands
    return l;
}

namespace {

// One direction of the status line's position, or "" if the image fits that way.
std::string position(double at, double max, const char* start, const char* end, const char* along) {
    if (max <= 0) return "";
    if (at <= 0) return start;
    if (at >= max) return end;
    return std::to_string(std::lround(100 * at / max)) + "%" + along;
}

// "bars 5-9 of 120", or "bar 5 of 120" when only one is on screen.
std::string range(const char* one, const char* many, std::pair<int, int> shown, int count) {
    const std::string of = " of " + std::to_string(count);
    if (shown.first == shown.second) return std::string(one) + " " + std::to_string(shown.first) + of;
    return std::string(many) + " " + std::to_string(shown.first) + "-" + std::to_string(shown.second) + of;
}

}  // namespace

std::string statusLine(const std::string& name, const View& v, int width) {
    std::string where, keys;
    if (v.hasStops()) {
        where = range("bar", "bars", v.barsOnScreen(), v.barCount());
        if (v.staffCount() > 0) where += "  " + range("staff", "staves", v.stavesOnScreen(), v.staffCount());
        keys = "arrows/hjkl bar/staff  Shift+arrows/PgUp/PgDn page  Home/End  +/- zoom  q quit";
    } else {
        const std::string across = position(v.x(), v.maxX(), "left", "right", " across");
        const std::string down = position(v.y(), v.maxY(), "top", "bottom", " down");
        if (across.empty() && down.empty())
            where = "all";
        else
            where = across + (across.empty() || down.empty() ? "" : ", ") + down;
        keys = "arrows/hjkl move  PgUp/PgDn page  Home/End  +/- zoom  q quit";
    }
    const long zoomPercent = std::lround(v.zoom() / v.startZoom() * 100);
    std::string s = " " + name + "  " + where + "  zoom " + std::to_string(zoomPercent) + "%    " + keys;
    s.resize(size_t(std::max(0, width)), ' ');
    return s;
}

double readableStaffSpace(Output output, int cellH) {
    if (output == Output::Braille) return 4;
    return 2.5 * (cellH > 0 ? cellH : kGuessCellH) / 4;
}

double scoreStartZoom(double docW, double docH, double staffSpaceUnits, const PagerLayout& layout, double spacePx) {
    const double fitWidthScale = layout.imageW / docW;  // pixels per unit at zoom 1
    double scale = spacePx / staffSpaceUnits;
    const double fitHeightScale = layout.imageH / docH;
    if (fitHeightScale > scale) scale = std::min(fitHeightScale, 2 * scale);
    return scale / fitWidthScale;
}

View::Stops scoreStops(const ScoreLayout& layout, double docHeight) {
    View::Stops s;
    s.bars = layout.barStarts;
    s.barsEnd = layout.end;
    for (const ScoreLayout::Staff& staff : layout.staves) s.staves.push_back(staff.bandTop);
    s.stavesEnd = docHeight;
    s.margin = layout.pinRight + 0.8 * layout.staffSpace;
    return s;
}

View startView(const SvgDocument& doc, const PagerOptions& opt, const PagerLayout& layout, int cellH) {
    double zoom = 1;
    if (opt.startWidth > 0)
        zoom = double(opt.startWidth) / layout.imageW;
    else if (opt.staffSpaceUnits > 0)
        zoom = scoreStartZoom(doc.width(), doc.height(), opt.staffSpaceUnits, layout,
                              readableStaffSpace(opt.output, cellH));
    View view(doc.width(), doc.height(), layout.imageW, layout.imageH, zoom);
    if (!opt.stops.bars.empty()) {
        view.setStops(opt.stops);
        view.goTo(opt.startBar, opt.startStaff);
    }
    return view;
}

bool runPager(const SvgDocument& doc, const TerminalInfo& term, const PagerOptions& opt) {
    RawInput in;
    if (!in.ok()) return false;

    WindowSize size = queryWindowSize();
    int cols = size.cols > 0 ? size.cols : 80, rows = size.rows > 0 ? size.rows : 24;
    PagerLayout layout = pagerLayout(cols, rows, term.cellW, term.cellH, opt.output);
    View view = startView(doc, opt, layout, term.cellH);

    FullScreen fullScreen(opt.output);
    bool dirty = true, clear = true;
    std::string pending;
    for (;;) {
        if (dirty) {
            drawFrame(doc, view, layout, cols, rows, opt, clear);
            dirty = clear = false;
        }

        // Handle every key that has arrived before drawing again, so held-down keys don't lag.
        const bool got = in.read(pending, kPollMs);
        bool quit = false;
        for (Key k; !quit && (k = takeKey(pending)) != Key::None;) {
            if (k == Key::Quit)
                quit = true;
            else
                dirty |= view.apply(k);
        }
        if (!quit && !got && !pending.empty()) quit = flushPending(pending) == Key::Quit;
        if (quit) break;

        size = queryWindowSize();
        if (size.cols > 0 && size.rows > 0 && (size.cols != cols || size.rows != rows)) {
            cols = size.cols;
            rows = size.rows;
            layout = pagerLayout(cols, rows, term.cellW, term.cellH, opt.output);
            view.resize(layout.imageW, layout.imageH);
            dirty = clear = true;
        }
    }
    return true;
}
