#include <lunasvg.h>

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
#include "testpattern.hpp"

namespace {

const char* const kUsage =
    "usage: stave --svg FILE [--width N]\n"
    "       stave --test-pattern\n"
    "  --svg FILE      draw an SVG file in the terminal\n"
    "  --width N       how wide to draw it, in pixels (default 800)\n"
    "  --test-pattern  draw a test image, to check that the terminal can show sixel graphics\n"
    "  --version       print the versions of stave and its libraries\n";

constexpr int kDefaultWidth = 800;  // until we can ask the terminal how wide it is

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
    bool testPatternRequested = false;
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
    if (testPatternRequested && svgPath) return usageError("use either --svg or --test-pattern, not both");
    if (width && !svgPath) return usageError("--width only applies to --svg");
    if (!testPatternRequested && !svgPath) {
        std::fputs(kUsage, stdout);
        return 0;
    }

    std::optional<Canvas> canvas;
    if (svgPath) {
        SvgRenderResult r = renderSvgFile(*svgPath, width.value_or(kDefaultWidth));
        if (!r.canvas) {
            std::fprintf(stderr, "stave: %s\n", r.error.c_str());
            return 1;
        }
        canvas = std::move(r.canvas);
    } else {
        canvas = testPattern();
    }

    ConsoleSetup console;
    const RGB ink{0, 0, 0}, paper{255, 255, 255};  // until we can ask the terminal for its colours
    if (!writeAll(encodeSixel(*canvas, ink, paper, /*opaque=*/true) + "\n")) {
        std::fputs("stave: couldn't write to stdout\n", stderr);
        return 1;
    }
    return 0;
}
