#include <lunasvg.h>
#include <toolkit.h>  // Verovio

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "encode.hpp"
#include "pager.hpp"
#include "score.hpp"
#include "svgimage.hpp"
#include "terminal.hpp"
#include "termprobe.hpp"
#include "testpattern.hpp"

namespace {

const char* const kUsage =
    "usage: stave FILE [--width N] [--print]\n"
    "       stave --test-pattern\n"
    "       stave --terminal-info\n"
    "  FILE             a score (MusicXML .musicxml/.xml/.mxl, MEI .mei or ABC .abc) or an SVG (.svg),\n"
    "                   shown full screen: arrows/hjkl move, PgUp/PgDn page, +/- zoom, q quits\n"
    "  --svg FILE       treat FILE as an SVG, whatever its extension\n"
    "  --width N        how wide to draw it at first, in pixels\n"
    "  --bar N, --staff N  for a score: start at this bar and staff (counting from 1)\n"
    "  --print          print it instead, as wide as the terminal (always the case when output isn't a\n"
    "                   terminal)\n"
    "  --verovio-data DIR  where Verovio's music fonts are (default: verovio-data next to stave)\n"
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

bool isSvgFile(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".svg";
}

// Loads FILE: an SVG as it is, or a score engraved by Verovio into SVG.
std::unique_ptr<SvgDocument> loadDocument(const std::string& path, bool isSvg, const std::string& dataDir,
                                          std::string& error) {
    if (isSvg) return SvgDocument::fromFile(path, error);
    const EngraveResult score = engraveScore(path, dataDir);
    if (!score.error.empty()) {
        error = score.error;
        return nullptr;
    }
    auto doc = SvgDocument::fromData(score.svg, error);
    if (!doc) {
        error = path + ": Verovio's drawing couldn't be read: " + error;
        return nullptr;
    }
    doc->splitInto(".system");  // draw only the bars on screen, so long scores scroll smoothly
    return doc;
}

int run(int argc, char** argv) {
    bool testPatternRequested = false, infoRequested = false, printRequested = false, forceSvg = false;
    std::optional<std::string> file, dataDir;
    std::optional<int> width, startBar, startStaff;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--version") {
            std::printf("stave %s\nlunasvg %s\nverovio %s\n", STAVE_VERSION, lunasvg_version_string(),
                        vrv::Toolkit(/*initFont=*/false).GetVersion().c_str());
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (arg == "--test-pattern") {
            testPatternRequested = true;
        } else if (arg == "--terminal-info") {
            infoRequested = true;
        } else if (arg == "--print") {
            printRequested = true;
        } else if (arg == "--svg") {
            if (!hasValue) return usageError("--svg needs a file name");
            if (file) return usageError("give only one file");
            file = argv[++i];
            forceSvg = true;
        } else if (arg == "--verovio-data") {
            if (!hasValue) return usageError("--verovio-data needs a folder");
            dataDir = argv[++i];
        } else if (arg == "--width") {
            if (!hasValue) return usageError("--width needs a number of pixels");
            width = parseInt(argv[++i]);
            if (!width || *width < 1 || *width > kMaxImageSide)
                return usageError("--width needs a whole number from 1 to " + std::to_string(kMaxImageSide) +
                                  ", not '" + argv[i] + "'");
        } else if (arg == "--bar" || arg == "--staff") {
            if (!hasValue) return usageError(std::string(arg) + " needs a number");
            std::optional<int>& target = arg == "--bar" ? startBar : startStaff;
            target = parseInt(argv[++i]);
            if (!target || *target < 1)
                return usageError(std::string(arg) + " needs a whole number from 1, not '" + argv[i] + "'");
        } else if (!arg.empty() && arg[0] == '-') {
            return usageError("unknown option '" + std::string(arg) + "'");
        } else {
            if (file) return usageError("give only one file");
            file = std::string(arg);
        }
    }
    if (int(testPatternRequested) + int(file.has_value()) + int(infoRequested) > 1)
        return usageError("use only one of FILE, --test-pattern and --terminal-info");
    if ((width || printRequested || dataDir) && !file)
        return usageError("--width, --print and --verovio-data only apply when showing a file");
    if ((startBar || startStaff) && (!file || forceSvg || isSvgFile(*file)))
        return usageError("--bar and --staff only apply to scores");
    if (!testPatternRequested && !file && !infoRequested) {
        std::fputs(kUsage, stdout);
        return 0;
    }

    // Load before touching the terminal, so a bad file is reported plainly.
    std::unique_ptr<SvgDocument> doc;
    const bool isSvg = file && (forceSvg || isSvgFile(*file));
    if (file) {
        std::string error;
        doc = loadDocument(*file, isSvg, dataDir.value_or(defaultVerovioDataDir()), error);
        if (!doc) {
            std::fprintf(stderr, "stave: %s\n", error.c_str());
            return 1;
        }
    }
    std::optional<ScoreLayout> layout;
    if (doc && !isSvg) {
        layout = findScoreLayout(*doc);
        const int bars = layout ? int(layout->barStarts.size()) : 0, staves = layout ? int(layout->staves.size()) : 0;
        if (layout && (startBar.value_or(1) > bars || startStaff.value_or(1) > staves)) {
            std::fprintf(stderr, "stave: %s has %d bar%s and %d stave%s\n", file->c_str(), bars, bars == 1 ? "" : "s",
                         staves, staves == 1 ? "" : "s");
            return 1;
        }
    }

    ConsoleSetup console;
    const TerminalInfo term = probeTerminal();
    if (infoRequested) {
        printTerminalInfo(term);
        return 0;
    }
    const InkAndPaper colours = chooseColours(term);

    if (doc && !printRequested && isInteractive()) {
        PagerOptions opt;
        opt.name = std::filesystem::path(*file).filename().string();
        opt.ink = colours.ink;
        opt.paper = colours.paper;
        opt.startWidth = width.value_or(0);
        if (!isSvg) opt.staffSpaceUnits = layout ? layout->staffSpace : kStaffSpaceUnits;
        if (layout) {
            opt.stops = scoreStops(*layout, doc->height());
            opt.startBar = startBar.value_or(1);
            opt.startStaff = startStaff.value_or(1);
        }
        if (runPager(*doc, term, opt)) return 0;
        // Couldn't read keys after all: fall through and print instead.
    }

    std::optional<Canvas> canvas;
    if (doc) {
        const int defaultWidth = term.pixelWidth() > 0 ? std::min(term.pixelWidth(), kMaxImageSide) : kFallbackWidth;
        SvgRenderResult r = renderSvgDocument(*doc, width.value_or(defaultWidth));
        if (!r.canvas) {
            std::fprintf(stderr, "stave: %s: %s\n", file->c_str(), r.error.c_str());
            return 1;
        }
        canvas = std::move(r.canvas);
    } else {
        canvas = testPattern();
    }

    if (!writeAll(encodeSixel(*canvas, colours.ink, colours.paper, colours.opaque) + "\n")) {
        std::fputs("stave: couldn't write to stdout\n", stderr);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Catching here unwinds the stack, so the pager and console guards put the terminal back.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "stave: %s\n", e.what());
        return 1;
    }
}
