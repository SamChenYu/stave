#include <lunasvg.h>
#include <toolkit.h>  // Verovio

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "compare.hpp"
#include "encode.hpp"
#include "marks.hpp"
#include "notemarks.hpp"
#include "pager.hpp"
#include "platform.hpp"
#include "score.hpp"
#include "svgimage.hpp"
#include "terminal.hpp"
#include "termprobe.hpp"
#include "testpattern.hpp"

namespace {

const char* const kUsage =
    "usage: stave FILE [--width N] [--print]\n"
    "       stave --compare OLD NEW\n"
    "       stave --test-pattern\n"
    "       stave --terminal-info\n"
    "  FILE             a score (MusicXML .musicxml/.xml/.mxl, MEI .mei or ABC .abc) or an SVG (.svg),\n"
    "                   shown full screen: arrows/hjkl move, PgUp/PgDn page, +/- zoom, n/N change, q quits\n"
    "  --svg FILE       treat FILE as an SVG, whatever its extension\n"
    "  --width N        how wide to draw it at first, in pixels\n"
    "  --bar N, --staff N  for a score: start at this bar (its printed number) and staff (counting\n"
    "                   from 1)\n"
    "  --added BARS, --changed BARS, --removed BARS\n"
    "                   for a score: draw these bars in green, yellow or red, to show a diff. BARS is\n"
    "                   bar numbers and ranges separated by commas, each on every staff or one (staves\n"
    "                   counting from 1): --added 40-60 --changed 12-15@2,20@3 --removed 7\n"
    "  --marks FILE.json  for a score: more marks, of bars or of single notes, from a file: a list like\n"
    "                   [{\"tone\": \"changed\", \"bar\": 12, \"staff\": 2, \"at\": \"3/2\", \"pitch\": 66}]\n"
    "  --name NAME      what the status line calls the file (default: its name), like \"song @ HEAD~1\"\n"
    "  --compare OLD NEW  two versions of a score side by side, the old on the left, scrolling together;\n"
    "                   --marks, --added and the like mark the new one, --bar and --staff are its own\n"
    "  --before-marks FILE.json  with --compare: marks for the old version, like --marks\n"
    "  --before-name NAME  with --compare: what the status line calls the old version\n"
    "  --pairs RUNS     with --compare: which bars of the old version are which of the new, as runs of\n"
    "                   OLD=NEW bars, like 1-17=1-17,19-40=18-39 (bar 18 deleted); by default, in order\n"
    "  --sixel, --kitty, --braille  how to draw (default: the best this terminal can show)\n"
    "  --paper          dark ink on a cream page, instead of the terminal's own colours\n"
    "  --png OUT.png    save the first screen (1600x900, dark ink on a cream page) instead\n"
    "  --bench          time loading the file, drawing a screen, and each way of sending it\n"
    "  --print          print it instead, as wide as the terminal (always the case when output isn't a\n"
    "                   terminal)\n"
    "  --verovio-data DIR  where Verovio's music fonts are (default: verovio-data next to stave)\n"
    "  --test-pattern   draw a test image, to check that the terminal can show sixel graphics\n"
    "  --terminal-info  show what stave found out about this terminal\n"
    "  --version        print the versions of stave and its libraries\n"
    "Set STAVE_DUMP_SVG=FILE to save the SVG Verovio draws a score as, for debugging.\n";

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
    std::printf("theme:      red %s, green %s, yellow %s\n", describe(t.red).c_str(), describe(t.green).c_str(),
                describe(t.yellow).c_str());
    std::printf("drawing:    %s\n", t.fg && t.bg ? "in the terminal's text colour, on its own background"
                                                 : "black on a white page (the terminal's colours are unknown)");
    std::vector<std::string> notes;
    const InkAndPaper c = chooseColours(t, &notes);
    std::printf("marks:      added %s, changed %s, removed %s\n", describe(c.added).c_str(),
                describe(c.changed).c_str(), describe(c.removed).c_str());
    for (const std::string& note : notes) std::printf("            %s\n", note.c_str());
    std::printf("graphics:   sixel %s, kitty %s%s\n", t.sixel ? "yes" : "no", t.kitty ? "yes" : "no",
                t.answered ? "" : " (the terminal didn't answer)");
    std::printf("output:     %s\n", outputName(chooseOutput(t)));
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

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Loads FILE: an SVG as it is, or a score engraved by Verovio into SVG. engraveMs is set to how
// long Verovio took, for --bench, measureNumbers to a score's printed bar numbers, and with
// readNotes, notes to its notes.
std::unique_ptr<SvgDocument> loadDocument(const std::string& path, bool isSvg, const std::string& dataDir,
                                          std::string& error, double& engraveMs,
                                          std::vector<std::string>& measureNumbers, bool readNotes,
                                          std::vector<ScoreNote>& notes) {
    engraveMs = 0;
    if (isSvg) return SvgDocument::fromFile(path, error);
    const auto start = Clock::now();
    const EngraveResult score = engraveScore(path, dataDir, readNotes);
    engraveMs = millisecondsSince(start);
    if (!score.error.empty()) {
        error = score.error;
        return nullptr;
    }
    if (const std::optional<std::string> dump = environmentVariable("STAVE_DUMP_SVG")) {
        std::ofstream out(*dump, std::ios::binary);
        out << score.svg;
        if (out)
            std::fprintf(stderr, "stave: saved Verovio's SVG to %s\n", dump->c_str());
        else
            std::fprintf(stderr, "stave: couldn't save Verovio's SVG to %s\n", dump->c_str());
    }
    auto doc = SvgDocument::fromData(score.svg, error);
    if (!doc) {
        error = path + ": Verovio's drawing couldn't be read: " + error;
        return nullptr;
    }
    measureNumbers = score.measureNumbers;
    notes = score.notes;
    return doc;
}

// --bench: how long each stage takes, on a 1920x1080 screen with 20-pixel text rows.
int bench(const SvgDocument& doc, const PagerOptions& opt, const std::optional<ScoreLayout>& layout, double engraveMs,
          double loadMs) {
    constexpr int kRuns = 50;
    const PagerLayout screen{53, 1920, 1080};
    View v = startView(doc, opt, screen, 20);
    if (engraveMs > 0) std::printf("engrave   %8.1f ms  (Verovio)\n", engraveMs);
    std::printf("load      %8.1f ms  (reading the SVG%s)\n", loadMs - engraveMs,
                layout ? ", finding the bars and staves" : "");
    if (layout) {
        const size_t bars = layout->barStarts.size(), staves = layout->staves.size();
        std::printf("          %zu bar%s, %zu stave%s\n", bars, bars == 1 ? "" : "s", staves, staves == 1 ? "" : "s");
    }

    // Each run draws the next screen across, as paging through would, then starts over at the end.
    std::vector<Canvas> frames;
    double renderMs = 0;
    for (int i = 0; i < kRuns; ++i) {
        const auto start = Clock::now();
        frames.push_back(doc.render(v.scale(), v.x(), v.y(), screen.imageW, screen.imageH));
        renderMs += millisecondsSince(start);
        if (!v.apply(Key::PageRight)) v.apply(Key::Home);
    }
    size_t bytes = 0;  // also keeps the compiler from skipping the work
    auto time = [&](auto&& encode) {
        const auto start = Clock::now();
        for (const Canvas& c : frames) bytes += encode(c);
        return millisecondsSince(start) / kRuns;
    };
    const Inks ink = kPaperColours.inks();
    const RGB paper = kPaperColours.paper;
    std::printf("screen    %dx%d, average of %d:\n", screen.imageW, screen.imageH, kRuns);
    std::printf("  draw    %8.2f ms\n", renderMs / kRuns);
    std::printf("  sixel   %8.2f ms\n", time([&](const Canvas& c) { return encodeSixel(c, ink, paper, true).size(); }));
    std::printf("  kitty   %8.2f ms\n",
                time([&](const Canvas& c) { return encodeKitty(c, ink, paper, false, false).size(); }));
    std::printf("  braille %8.2f ms\n", time([&](const Canvas& c) { return encodeBraille(c).size(); }));
    return bytes == 0 ? 1 : 0;
}

// Reads a --marks file into `bars` and `notes`. Returns "" if all is well, otherwise what's wrong.
std::string readMarksFile(const std::string& path, std::vector<Mark>& bars, std::vector<NoteMark>& notes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "can't open " + path;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    if (!parseMarksFile(text, path, bars, notes, error)) return error;
    return "";
}

// Colours a score's marks: finds the bars and notes they name and sets them on the document.
// Returns "" if all is well, otherwise what's wrong with the first mark that isn't.
std::string applyMarks(SvgDocument& doc, const ScoreLayout& layout, const BarNumbers& numbers,
                       std::vector<Mark>& bars, const std::vector<NoteMark>& notes,
                       const std::vector<ScoreNote>& scoreNotes) {
    if (std::string error = checkMarks(bars, numbers, int(layout.staves.size())); !error.empty()) return error;
    doc.setToneRegions(toneRegions(bars, layout, doc.width(), doc.height()));
    std::vector<std::pair<std::string, Tone>> elements;
    if (std::string error = findNoteMarks(notes, scoreNotes, layout, numbers, bars, elements); !error.empty())
        return error;
    doc.setToneElements(elements);
    return "";
}

// One version of a score, engraved, laid out and marked, ready to page through.
struct MarkedScore {
    std::unique_ptr<SvgDocument> doc;
    ScoreLayout layout;
    BarNumbers numbers{{}, 0};
    std::vector<std::pair<int, int>> changes;  // where its marks start, for n and N
};

// Engraves `path` and colours its marks. On failure, returns nothing with the reason in `error`.
std::optional<MarkedScore> loadMarkedScore(const std::string& path, const std::string& dataDir,
                                           std::vector<Mark>& bars, const std::vector<NoteMark>& notes,
                                           std::string& error) {
    double engraveMs = 0;
    std::vector<std::string> measureNumbers;
    std::vector<ScoreNote> scoreNotes;
    MarkedScore s;
    s.doc = loadDocument(path, /*isSvg=*/false, dataDir, error, engraveMs, measureNumbers, !notes.empty(),
                         scoreNotes);
    if (!s.doc) return std::nullopt;
    const std::optional<ScoreLayout> layout = findScoreLayout(*s.doc);
    if (!layout) {
        error = "can't find the bars in " + path;
        return std::nullopt;
    }
    s.layout = *layout;
    prepareForPaging(*s.doc, s.layout);
    s.numbers = BarNumbers::of(s.layout, measureNumbers);
    if (std::string e = applyMarks(*s.doc, s.layout, s.numbers, bars, notes, scoreNotes); !e.empty()) {
        error = e;
        return std::nullopt;
    }
    s.changes = changeStarts(bars, notes, s.numbers);
    return s;
}

// What --compare was asked to show.
struct CompareRequest {
    std::string beforeFile, afterFile, dataDir;
    std::string beforeName, afterName;
    std::vector<Mark> beforeMarks;
    std::vector<NoteMark> beforeNoteMarks;
    std::vector<Mark> afterMarks;
    std::vector<NoteMark> afterNoteMarks;
    std::vector<BarRun> pairs;
    std::optional<int> startBar, startStaff;  // on the new version
    std::optional<Output> forcedOutput;
    bool paper = false;
    std::optional<std::string> pngPath;
};

// The pager's options for one version of a comparison.
PagerOptions compareSide(const MarkedScore& s, const std::string& name) {
    PagerOptions opt;
    opt.name = name;
    opt.staffSpaceUnits = s.layout.staffSpace;
    opt.stops = scoreStops(s.layout, s.doc->height());
    opt.barLabels = s.numbers.labels();
    return opt;
}

// --compare: two versions side by side.
int runCompare(CompareRequest& r) {
    std::string error;
    std::optional<MarkedScore> before = loadMarkedScore(r.beforeFile, r.dataDir, r.beforeMarks, r.beforeNoteMarks, error);
    if (!before) {
        std::fprintf(stderr, "stave: %s\n", error.c_str());
        return 1;
    }
    std::optional<MarkedScore> after = loadMarkedScore(r.afterFile, r.dataDir, r.afterMarks, r.afterNoteMarks, error);
    if (!after) {
        std::fprintf(stderr, "stave: %s\n", error.c_str());
        return 1;
    }

    CompareOptions opt;
    if (const std::string e = mapBars(r.pairs, before->numbers, after->numbers, opt.bars); !e.empty()) {
        std::fprintf(stderr, "stave: %s\n", e.c_str());
        return 1;
    }
    opt.before = compareSide(*before, r.beforeName);
    opt.after = compareSide(*after, r.afterName);

    // n and N go to every change, on either side, at the new version's bars.
    std::vector<std::pair<int, int>> changes = after->changes;
    for (const auto& [bar, staff] : before->changes)
        if (size_t(bar) < opt.bars.oldToNew.size()) changes.emplace_back(opt.bars.oldToNew[size_t(bar)], staff);
    std::sort(changes.begin(), changes.end());
    changes.erase(std::unique(changes.begin(), changes.end()), changes.end());
    opt.after.stops.changes = changes;

    if (r.startBar) {
        const std::optional<std::pair<int, int>> found = after->numbers.find(*r.startBar);
        if (!found) {
            std::fprintf(stderr, "stave: %s has no bar %d; %s\n", r.afterFile.c_str(), *r.startBar,
                         after->numbers.describe().c_str());
            return 1;
        }
        opt.after.startBar = found->first + 1;
    }
    opt.after.startStaff = r.startStaff.value_or(1);

    if (r.pngPath) {
        // The first screen on a 1600x900 display with 10x20-pixel characters, as --png draws a score.
        CompareLayout screen{};
        screen.screen = PagerLayout{45, 1600, 900};
        screen.split = splitWidths(1600, 10);
        screen.leftCols = screen.split.left / 10;
        const CompareViews views = startCompareViews(*before->doc, *after->doc, opt, screen, 20);
        const Canvas c = renderCompare(*before->doc, *after->doc, views, screen);
        if (!writePng(c, kPaperColours.inks(), kPaperColours.paper, *r.pngPath)) {
            std::fprintf(stderr, "stave: couldn't write %s\n", r.pngPath->c_str());
            return 1;
        }
        std::printf("wrote %s (%dx%d)\n", r.pngPath->c_str(), c.w, c.h);
        return 0;
    }

    ConsoleSetup console;
    const TerminalInfo term = probeTerminal();
    const InkAndPaper colours = r.paper ? kPaperColours : chooseColours(term);
    opt.after.output = r.forcedOutput.value_or(chooseOutput(term));
    opt.after.inks = colours.inks();
    opt.after.brailleColours = toneColours(colours.inks(), colours.exact);
    opt.after.paper = colours.paper;
    opt.after.transparent = !colours.opaque;
    if (!isInteractive() || !runComparePager(*before->doc, *after->doc, term, opt)) {
        std::fputs("stave: --compare needs an interactive terminal, or --png\n", stderr);
        return 1;
    }
    return 0;
}

int run(int argc, char** argv) {
    bool testPatternRequested = false, infoRequested = false, printRequested = false, forceSvg = false;
    bool paperRequested = false, benchRequested = false;
    std::optional<Output> forcedOutput;
    std::optional<std::string> file, dataDir, pngPath, shownName;
    std::optional<int> width, startBar, startStaff;
    std::vector<Mark> marks;
    std::vector<NoteMark> noteMarks;
    bool marksFileGiven = false;
    // --compare: the old version, and what to show on it
    std::optional<std::string> beforeFile, beforeName;
    std::vector<Mark> beforeMarks;
    std::vector<NoteMark> beforeNoteMarks;
    bool beforeMarksGiven = false, pairsGiven = false;
    std::vector<BarRun> pairs;
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
        } else if (arg == "--sixel" || arg == "--kitty" || arg == "--braille") {
            forcedOutput = arg == "--sixel" ? Output::Sixel : arg == "--kitty" ? Output::Kitty : Output::Braille;
        } else if (arg == "--bench") {
            benchRequested = true;
        } else if (arg == "--paper") {
            paperRequested = true;
        } else if (arg == "--print") {
            printRequested = true;
        } else if (arg == "--svg") {
            if (!hasValue) return usageError("--svg needs a file name");
            if (file) return usageError("give only one file");
            file = argv[++i];
            forceSvg = true;
        } else if (arg == "--png") {
            if (!hasValue) return usageError("--png needs a file name to save to");
            pngPath = argv[++i];
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
            const int least = arg == "--bar" ? 0 : 1;  // a pickup is often bar 0
            if (!target || *target < least)
                return usageError(std::string(arg) + " needs a whole number from " + std::to_string(least) +
                                  ", not '" + argv[i] + "'");
        } else if (arg == "--added" || arg == "--changed" || arg == "--removed") {
            const Tone tone = arg == "--added" ? Tone::Added : arg == "--changed" ? Tone::Changed : Tone::Removed;
            if (!hasValue) return usageError(std::string(arg) + " needs bars, like 7, 12-15 or 12-15@2");
            std::string error;
            if (!parseMarks(argv[++i], tone, marks, error)) return usageError(error);
        } else if (arg == "--name") {
            if (!hasValue) return usageError("--name needs a name to show");
            shownName = argv[++i];
        } else if (arg == "--marks" || arg == "--before-marks") {
            if (!hasValue) return usageError(std::string(arg) + " needs a file of marks");
            const bool before = arg == "--before-marks";
            if (const std::string error = readMarksFile(argv[++i], before ? beforeMarks : marks,
                                                        before ? beforeNoteMarks : noteMarks);
                !error.empty()) {
                std::fprintf(stderr, "stave: %s\n", error.c_str());
                return 1;
            }
            (before ? beforeMarksGiven : marksFileGiven) = true;
        } else if (arg == "--compare") {
            if (i + 2 >= argc) return usageError("--compare needs two scores: the old version, then the new");
            if (file) return usageError("give only one file, or --compare and two");
            beforeFile = argv[++i];
            file = argv[++i];
        } else if (arg == "--pairs") {
            if (!hasValue) return usageError("--pairs needs runs of bars, like 1-17=1-17,19-40=18-39");
            std::string error;
            if (!parsePairs(argv[++i], pairs, error)) return usageError(error);
            pairsGiven = true;
        } else if (arg == "--before-name") {
            if (!hasValue) return usageError("--before-name needs a name to show");
            beforeName = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            return usageError("unknown option '" + std::string(arg) + "'");
        } else {
            if (file) return usageError("give only one file");
            file = std::string(arg);
        }
    }
    if ((beforeMarksGiven || pairsGiven || beforeName) && !beforeFile)
        return usageError("--before-marks, --pairs and --before-name only apply with --compare");
    if (beforeFile) {
        if (forceSvg || isSvgFile(*beforeFile) || isSvgFile(*file)) return usageError("--compare only takes scores");
        if (printRequested || width || benchRequested)
            return usageError("--print, --width and --bench don't apply with --compare");
        if (testPatternRequested || infoRequested)
            return usageError("use only one of --compare, --test-pattern and --terminal-info");
        CompareRequest request{*beforeFile, *file, dataDir.value_or(defaultVerovioDataDir()),
                               beforeName.value_or(std::filesystem::path(*beforeFile).filename().string()),
                               shownName.value_or(std::filesystem::path(*file).filename().string()),
                               std::move(beforeMarks), std::move(beforeNoteMarks), std::move(marks),
                               std::move(noteMarks), std::move(pairs), startBar, startStaff,
                               forcedOutput, paperRequested, pngPath};
        return runCompare(request);
    }
    if (int(testPatternRequested) + int(file.has_value()) + int(infoRequested) > 1)
        return usageError("use only one of FILE, --test-pattern and --terminal-info");
    if ((width || printRequested || dataDir || pngPath || benchRequested || shownName) && !file)
        return usageError("--width, --name, --print, --png, --bench and --verovio-data only apply when showing a file");
    if ((startBar || startStaff || !marks.empty() || marksFileGiven) && (!file || forceSvg || isSvgFile(*file)))
        return usageError("--bar, --staff, --added, --changed, --removed and --marks only apply to scores");
    if (!testPatternRequested && !file && !infoRequested) {
        std::fputs(kUsage, stdout);
        return 0;
    }

    // Load before touching the terminal, so a bad file is reported plainly.
    std::unique_ptr<SvgDocument> doc;
    const bool isSvg = file && (forceSvg || isSvgFile(*file));
    Clock::time_point loadStart;
    double engraveMs = 0;
    std::vector<std::string> measureNumbers;
    std::vector<ScoreNote> scoreNotes;  // only read when there are notes to mark
    if (file) {
        std::string error;
        loadStart = Clock::now();
        doc = loadDocument(*file, isSvg, dataDir.value_or(defaultVerovioDataDir()), error, engraveMs,
                           measureNumbers, !noteMarks.empty(), scoreNotes);
        if (!doc) {
            std::fprintf(stderr, "stave: %s\n", error.c_str());
            return 1;
        }
    }
    std::optional<ScoreLayout> layout;
    std::optional<BarNumbers> numbers;  // a score's printed bar numbers
    int startIndex = 0;                 // the bar to start at, counting from 0
    if (doc && !isSvg) {
        layout = findScoreLayout(*doc);
        if (layout)
            prepareForPaging(*doc, *layout);
        else
            doc->splitInto(".system");  // still draw only what's on screen, if it's laid out in systems
        const int staves = layout ? int(layout->staves.size()) : 0;
        if (layout) {
            numbers = BarNumbers::of(*layout, measureNumbers);
            if (startBar) {
                const std::optional<std::pair<int, int>> found = numbers->find(*startBar);
                if (!found) {
                    std::fprintf(stderr, "stave: %s has no bar %d; %s\n", file->c_str(), *startBar,
                                 numbers->describe().c_str());
                    return 1;
                }
                startIndex = found->first;
            }
            if (startStaff.value_or(1) > staves) {
                std::fprintf(stderr, "stave: %s has only %d %s\n", file->c_str(), staves,
                             staves == 1 ? "staff" : "staves");
                return 1;
            }
        }
        if (!marks.empty() || !noteMarks.empty()) {
            if (!layout) {
                std::fprintf(stderr, "stave: can't find the bars in %s, so can't mark any\n", file->c_str());
                return 1;
            }
            if (const std::string error = applyMarks(*doc, *layout, *numbers, marks, noteMarks, scoreNotes);
                !error.empty()) {
                std::fprintf(stderr, "stave: %s\n", error.c_str());
                return 1;
            }
        }
    }

    // What the pager needs to know about the file. The terminal-dependent parts come later.
    PagerOptions opt;
    if (doc) {
        opt.name = shownName.value_or(std::filesystem::path(*file).filename().string());
        opt.startWidth = width.value_or(0);
        if (!isSvg) opt.staffSpaceUnits = layout ? layout->staffSpace : kStaffSpaceUnits;
        if (layout) {
            opt.stops = scoreStops(*layout, doc->height());
            opt.stops.changes = changeStarts(marks, noteMarks, *numbers);
            opt.startBar = startIndex + 1;
            opt.barLabels = numbers->labels();
            opt.startStaff = startStaff.value_or(1);
        }
    }

    const double loadMs = doc ? millisecondsSince(loadStart) : 0;
    if (benchRequested) return bench(*doc, opt, layout, engraveMs, loadMs);

    if (pngPath) {
        // The pager's first screen on a 1600x900 display with 20-pixel text rows.
        const PagerLayout screen{45, 1600, 900};
        const View v = startView(*doc, opt, screen, 20);
        const Canvas c = doc->render(v.scale(), v.x(), v.y(), screen.imageW, screen.imageH);
        if (!writePng(c, kPaperColours.inks(), kPaperColours.paper, *pngPath)) {
            std::fprintf(stderr, "stave: couldn't write %s\n", pngPath->c_str());
            return 1;
        }
        std::printf("wrote %s (%dx%d)\n", pngPath->c_str(), c.w, c.h);
        return 0;
    }

    ConsoleSetup console;
    const TerminalInfo term = probeTerminal();
    if (infoRequested) {
        printTerminalInfo(term);
        return 0;
    }
    const InkAndPaper colours = paperRequested ? kPaperColours : chooseColours(term);
    const Output output = forcedOutput.value_or(chooseOutput(term));

    if (doc && !printRequested && isInteractive()) {
        opt.output = output;
        opt.inks = colours.inks();
        opt.brailleColours = toneColours(colours.inks(), colours.exact);
        opt.paper = colours.paper;
        opt.transparent = !colours.opaque;
        if (runPager(*doc, term, opt)) return 0;
        // Couldn't read keys after all: fall through and print instead.
    }

    std::optional<Canvas> canvas;
    if (doc) {
        // Braille's pixels are dots, two to a character; the others draw real pixels.
        int defaultWidth = term.pixelWidth() > 0 ? std::min(term.pixelWidth(), kMaxImageSide) : kFallbackWidth;
        if (output == Output::Braille) defaultWidth = 2 * (term.cols > 0 ? term.cols : 80);
        SvgRenderResult r = renderSvgDocument(*doc, width.value_or(defaultWidth));
        if (!r.canvas) {
            std::fprintf(stderr, "stave: %s: %s\n", file->c_str(), r.error.c_str());
            return 1;
        }
        canvas = std::move(r.canvas);
    } else {
        canvas = testPattern();
    }

    std::string image;
    switch (output) {
        case Output::Sixel: image = encodeSixel(*canvas, colours.inks(), colours.paper, colours.opaque); break;
        case Output::Kitty:
            image = encodeKitty(*canvas, colours.inks(), colours.paper, colours.opaque, /*moveCursor=*/true);
            break;
        case Output::Braille:
            for (const std::string& line : encodeBraille(*canvas, toneColours(colours.inks(), colours.exact)))
                image += line + "\n";
            if (!image.empty()) image.pop_back();
            break;
    }
    if (!writeAll(image + "\n")) {
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
