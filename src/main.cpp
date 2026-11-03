#include <lunasvg.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "encode.hpp"
#include "svgimage.hpp"
#include "terminal.hpp"
#include "termprobe.hpp"
#include "testpattern.hpp"

namespace {

const char* const kUsage =
    "usage: stave --svg FILE [--width N]\n"
    "       stave --test-pattern\n"
    "       stave --terminal-info\n"
    "  --svg FILE       draw an SVG file in the terminal\n"
    "  --width N        how wide to draw it, in pixels (default: the terminal's width)\n"
    "  --test-pattern   draw a test image, to check that the terminal can show sixel graphics\n"
    "  --terminal-info  show what stave found out about this terminal\n"
    "  --version        print the versions of stave and its libraries\n";

constexpr int kFallbackWidth = 800;  // when the terminal doesn't say how wide it is

std::string describe(const std::optional<RGB>& c) {
    if (!c) return "unknown";
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c->r, c->g, c->b);
    return buf;
}

void printTerminalInfo(const TerminalInfo& t) {
    if (t.cols > 0)
        std::printf("size:       %d x %d characters\n", t.cols, t.rows);
    else
        std::printf("size:       unknown (not running in a terminal?)\n");
    if (t.cellW > 0)
        std::printf("cell:       %d x %d pixels, so %d pixels wide\n", t.cellW, t.cellH, t.pixelWidth());
    else
        std::printf("cell:       unknown, so images default to %d pixels wide\n", kFallbackWidth);
    std::printf("text:       %s\n", describe(t.fg).c_str());
    std::printf("background: %s\n", describe(t.bg).c_str());
    std::printf("drawing:    %s\n", t.fg && t.bg ? "in the terminal's text colour, on its own background"
                                                 : "black on a white page (the terminal's colours are unknown)");
}

int usageError(const std::string& message) {
    std::fprintf(stderr, "stave: %s\n\n%s", message.c_str(), kUsage);
    return 2;
}

std::optional<int> parseInt(std::string_view s) {
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) return std::nullopt;
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    bool testPatternRequested = false, infoRequested = false;
    std::optional<std::string> svgPath;
    std::optional<int> width;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--version") {
            std::printf("stave %s\nlunasvg %s\n", STAVE_VERSION, lunasvg_version_string());
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (arg == "--test-pattern") {
            testPatternRequested = true;
        } else if (arg == "--terminal-info") {
            infoRequested = true;
        } else if (arg == "--svg") {
            if (!hasValue) return usageError("--svg needs a file name");
            svgPath = argv[++i];
        } else if (arg == "--width") {
            if (!hasValue) return usageError("--width needs a number of pixels");
            width = parseInt(argv[++i]);
            if (!width || *width < 1 || *width > kMaxImageSide)
                return usageError("--width needs a whole number from 1 to " + std::to_string(kMaxImageSide) +
                                  ", not '" + argv[i] + "'");
        } else {
            return usageError("unknown option '" + std::string(arg) + "'");
        }
    }
    if (int(testPatternRequested) + int(svgPath.has_value()) + int(infoRequested) > 1)
        return usageError("use only one of --svg, --test-pattern and --terminal-info");
    if (width && !svgPath) return usageError("--width only applies to --svg");
    if (!testPatternRequested && !svgPath && !infoRequested) {
        std::fputs(kUsage, stdout);
        return 0;
    }

    ConsoleSetup console;
    const TerminalInfo term = probeTerminal();
    if (infoRequested) {
        printTerminalInfo(term);
        return 0;
    }

    std::optional<Canvas> canvas;
    if (svgPath) {
        const int defaultWidth = term.pixelWidth() > 0 ? std::min(term.pixelWidth(), kMaxImageSide) : kFallbackWidth;
        SvgRenderResult r = renderSvgFile(*svgPath, width.value_or(defaultWidth));
        if (!r.canvas) {
            std::fprintf(stderr, "stave: %s\n", r.error.c_str());
            return 1;
        }
        canvas = std::move(r.canvas);
    } else {
        canvas = testPattern();
    }

    const InkAndPaper colours = chooseColours(term);
    if (!writeAll(encodeSixel(*canvas, colours.ink, colours.paper, colours.opaque) + "\n")) {
        std::fputs("stave: couldn't write to stdout\n", stderr);
        return 1;
    }
    return 0;
}
