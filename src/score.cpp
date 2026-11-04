#include "score.hpp"

#include <lunasvg.h>
#include <toolkit.h>  // Verovio
#include <toolkitdef.h>
// Verovio's document, for reading its notes
#include <doc.h>
#include <layer.h>
#include <measure.h>
#include <note.h>
#include <scoredef.h>
#include <staff.h>
#include <staffdef.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <map>
#include <system_error>

#include "platform.hpp"
#include "svgimage.hpp"

namespace fs = std::filesystem;

namespace {

EngraveResult failure(std::string message) { return {"", std::move(message), {}, {}}; }

// Verovio's toolkit, with its document opened up (it's protected) to read the notes from.
class NoteReadingToolkit : public vrv::Toolkit {
public:
    using vrv::Toolkit::Toolkit;

    std::vector<ScoreNote> notes() {
        // Each note's start within its bar is worked out with the timemap.
        if (!m_doc.HasTimemap()) m_doc.CalculateTimemap();
        std::map<const vrv::Object*, size_t> measureIndex;  // in document order, as in the SVG
        for (const vrv::Object* m : m_doc.FindAllDescendantsByType(vrv::MEASURE))
            measureIndex.emplace(m, measureIndex.size());
        const vrv::ScoreDef* scoreDef = m_doc.GetFirstScoreDef();

        std::vector<ScoreNote> out;
        for (const vrv::Object* o : m_doc.FindAllDescendantsByType(vrv::NOTE)) {
            const auto* note = dynamic_cast<const vrv::Note*>(o);
            const vrv::Object* measure = o->GetFirstAncestor(vrv::MEASURE);
            const auto* layer = dynamic_cast<const vrv::Layer*>(o->GetFirstAncestor(vrv::LAYER));
            const vrv::Staff* staff = note ? note->GetAncestorStaff(vrv::ANCESTOR_ONLY, false) : nullptr;
            if (!note || !measure || !staff || !measureIndex.count(measure)) continue;
            // Verovio's pitch is as written for a transposing instrument (and as sounding under an
            // 8va line), so add the staff's transposition, as its MIDI output does.
            int transposition = 0;
            if (const vrv::StaffDef* def = scoreDef ? scoreDef->GetStaffDef(staff->GetN()) : nullptr;
                def && def->HasTransSemi())
                transposition = def->GetTransSemi();
            const vrv::Fraction onset = note->GetScoreTimeOnset();
            ScoreNote n;
            n.id = note->GetID();
            if (const vrv::Object* chord = o->GetFirstAncestor(vrv::CHORD, 1)) n.chord = chord->GetID();
            n.measure = measureIndex.at(measure);
            n.staff = staff->GetN();
            n.voice = layer && layer->GetN() > 0 ? layer->GetN() : 1;
            n.onsetNum = onset.GetNumerator();
            n.onsetDen = onset.GetDenominator();
            n.midi = note->GetMIDIPitch(transposition, 0);
            out.push_back(std::move(n));
        }
        return out;
    }
};

// Verovio's error messages from the last load, on one line ("" if it gave none).
std::string verovioLog(vrv::Toolkit& tk) {
    std::string log = tk.GetLog();
    while (!log.empty() && (log.back() == '\n' || log.back() == '\r' || log.back() == ' ')) log.pop_back();
    for (char& c : log)
        if (c == '\n' || c == '\r') c = ' ';
    return log;
}

}  // namespace

std::string defaultVerovioDataDir() {
    const std::string dir = executableDir();
    return (fs::path(dir.empty() ? "." : dir) / "verovio-data").string();
}

EngraveResult engraveScore(const std::string& path, const std::string& dataDir, bool readNotes) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return failure("can't open " + path);
    if (!fs::is_directory(dataDir, ec))
        return failure("can't find Verovio's music fonts: no folder " + dataDir + " (use --verovio-data DIR)");

    // Collect Verovio's messages instead of letting it print them over the screen.
    vrv::EnableLogToBuffer(true);
    vrv::EnableLog(vrv::LOG_ERROR);

    // Symbols inside text (like the note in a tempo mark) use the music font as a text font.
    // lunasvg finds ordinary fonts (Times, for titles and lyrics) on the system, but not this one.
    static const bool leipzigLoaded =
        lunasvg_add_font_face_from_file("Leipzig", false, false, (fs::path(dataDir) / "Leipzig.ttf").string().c_str());
    (void)leipzigLoaded;

    NoteReadingToolkit tk(/*initFont=*/false);
    if (!tk.SetResourcePath(dataDir)) return failure("couldn't load Verovio's music fonts from " + dataDir);

    // One long system and no page furniture: the pager does the scrolling.
    tk.SetOptions(R"({
        "breaks": "none",
        "header": "none",
        "footer": "none",
        "adjustPageWidth": true,
        "adjustPageHeight": true,
        "smuflTextFont": "none",
        "svgAdditionalAttribute": ["measure@n"],
        "pageMarginTop": 50, "pageMarginBottom": 50, "pageMarginLeft": 50, "pageMarginRight": 50
    })");

    if (!tk.LoadFile(path)) {
        const std::string log = verovioLog(tk);
        return failure(path + ": not a score Verovio can read" + (log.empty() ? "" : " (" + log + ")"));
    }
    if (tk.GetPageCount() < 1) return failure(path + ": the score is empty");
    std::string svg = tk.RenderToSVG(1);
    if (svg.empty()) return failure(path + ": Verovio couldn't draw it");
    std::vector<std::string> numbers = measureNumbers(svg);
    return {std::move(svg), "", std::move(numbers), readNotes ? tk.notes() : std::vector<ScoreNote>()};
}

std::vector<std::string> measureNumbers(std::string_view svg) {
    std::vector<std::string> numbers;
    constexpr std::string_view kClass = "class=\"measure\"", kNumber = "data-n=\"";
    for (size_t at = svg.find(kClass); at != std::string_view::npos; at = svg.find(kClass, at + 1)) {
        const size_t open = svg.rfind('<', at), close = svg.find('>', at);
        if (open == std::string_view::npos || close == std::string_view::npos) break;
        const std::string_view tag = svg.substr(open, close - open);
        const size_t n = tag.find(kNumber);
        const size_t start = n == std::string_view::npos ? 0 : n + kNumber.size();
        const size_t end = n == std::string_view::npos ? 0 : tag.find('"', start);
        numbers.emplace_back(end == std::string_view::npos ? std::string_view() : tag.substr(start, end - start));
    }
    return numbers;
}

namespace {

std::vector<lunasvg::Element> childElements(const lunasvg::Element& e) {
    std::vector<lunasvg::Element> out;
    for (const lunasvg::Node& n : e.children())
        if (n.isElement()) out.push_back(n.toElement());
    return out;
}

bool hasClass(const lunasvg::Element& e, const std::string& cls) {
    const std::string& all = e.getAttribute("class");
    for (size_t at = all.find(cls); at != std::string::npos; at = all.find(cls, at + 1)) {
        const bool startOk = at == 0 || all[at - 1] == ' ';
        const bool endOk = at + cls.size() == all.size() || all[at + cls.size()] == ' ';
        if (startOk && endOk) return true;
    }
    return false;
}

// A staff's lines: the <path> children it starts with (then come clefs, layers and so on).
std::vector<lunasvg::Box> staffLines(const lunasvg::Element& staff) {
    std::vector<lunasvg::Box> lines;
    for (const lunasvg::Element& c : childElements(staff)) {
        if (c.getAttribute("d").empty()) break;
        lines.push_back(c.getGlobalBoundingBox());
    }
    return lines;
}

// Calls fn(x) with where every glyph (<use>) inside e is placed, in document units. A music
// font's glyph starts at its origin, so that's its left edge. (lunasvg's boxes for these glyphs
// aren't reliable enough here.)
template <class F>
void forEachGlyph(const lunasvg::Element& e, F&& fn) {
    for (const lunasvg::Element& c : childElements(e)) {
        if (c.hasAttribute("xlink:href") || c.hasAttribute("href"))
            fn(double(c.getGlobalMatrix().e));
        else
            forEachGlyph(c, fn);
    }
}

// Left and right edges, in document units.
struct Extent {
    double x0 = 1e300, x1 = -1e300;
    bool empty() const { return x0 > x1; }
};

// Grows `ext` to cover the shapes inside e. It goes by each shape's own box: lunasvg's box for a
// group also counts its empty children, at the page's corner.
void addShapeExtent(const lunasvg::Element& e, Extent& ext) {
    for (const lunasvg::Element& c : childElements(e)) {
        if (!childElements(c).empty()) {
            addShapeExtent(c, ext);
            continue;
        }
        const lunasvg::Box b = c.getGlobalBoundingBox();
        if (b.w <= 0 && b.h <= 0) continue;
        ext.x0 = std::min(ext.x0, double(b.x));
        ext.x1 = std::max(ext.x1, double(b.x + b.w));
    }
}

// Where the pinned column ends for one staff of the first bar: just past its last key-signature
// accidental (or its clef, without one), but never into the time signature. -1 if it has neither.
double staffPinRight(const lunasvg::Element& staff, double space) {
    double meter = 1e300, accid = -1, clef = -1;
    for (const lunasvg::Element& c : childElements(staff)) {
        if (hasClass(c, "meterSig")) forEachGlyph(c, [&](double x) { meter = std::min(meter, x); });
        if (hasClass(c, "keySig")) forEachGlyph(c, [&](double x) { accid = std::max(accid, x); });
        if (hasClass(c, "clef")) forEachGlyph(c, [&](double x) { clef = std::max(clef, x); });
    }
    double pin = accid >= 0 ? accid + 1.05 * space : clef >= 0 ? clef + 2.9 * space : -1;
    if (meter < 1e300) pin = pin >= 0 ? std::min(pin, meter - 0.3 * space) : meter - 0.3 * space;
    return pin;
}

}  // namespace

std::optional<ScoreLayout> findScoreLayout(const SvgDocument& doc) {
    const lunasvg::ElementList measures = doc.lunasvgDocument().querySelectorAll(".measure");
    if (measures.empty()) return std::nullopt;

    ScoreLayout layout;
    // Staves, from the first bar: the middle of each staff's top and bottom line.
    std::vector<double> spaces;
    for (const lunasvg::Element& staff : childElements(measures.front())) {
        if (!hasClass(staff, "staff")) continue;
        const std::vector<lunasvg::Box> lines = staffLines(staff);
        if (lines.empty()) continue;
        const double top = lines.front().y + lines.front().h / 2, bottom = lines.back().y + lines.back().h / 2;
        layout.staves.push_back({top, bottom, 0, 0});
        if (lines.size() >= 2) spaces.push_back((bottom - top) / double(lines.size() - 1));
    }
    if (layout.staves.empty()) return std::nullopt;
    if (!spaces.empty()) layout.staffSpace = spaces.front();
    for (size_t i = 0; i < layout.staves.size(); ++i) {
        ScoreLayout::Staff& s = layout.staves[i];
        s.bandTop = i == 0 ? 0 : (layout.staves[i - 1].bottom + s.top) / 2;
        s.bandBottom = i + 1 == layout.staves.size() ? doc.height() : (s.bottom + layout.staves[i + 1].top) / 2;
    }

    // Bars: where the first staff's lines start and end in each, and where each bar's barlines are.
    std::vector<std::vector<Extent>> barlines;  // one list for each of barStarts
    for (size_t measure = 0; measure < measures.size(); ++measure) {
        const lunasvg::Element& m = measures[measure];
        bool found = false;
        for (const lunasvg::Element& staff : childElements(m)) {
            if (!hasClass(staff, "staff")) continue;
            const std::vector<lunasvg::Box> lines = staffLines(staff);
            if (lines.empty()) continue;
            layout.barStarts.push_back(lines.front().x);
            layout.barMeasures.push_back(measure);
            layout.end = std::max(layout.end, double(lines.front().x + lines.front().w));
            found = true;
            break;
        }
        if (!found) continue;
        std::vector<Extent>& own = barlines.emplace_back();
        for (const lunasvg::Element& c : childElements(m)) {
            if (!hasClass(c, "barLine")) continue;
            Extent ext;
            addShapeExtent(c, ext);
            if (!ext.empty()) own.push_back(ext);
        }
    }
    if (layout.barStarts.empty()) return std::nullopt;

    // Inside each bar: trimmed by any barline (its own, or a neighbour's) that reaches into it, on
    // the side the barline is nearer.
    const size_t bars = layout.barStarts.size();
    for (size_t i = 0; i < bars; ++i) {
        const double start = layout.barStarts[i], stop = i + 1 < bars ? layout.barStarts[i + 1] : layout.end;
        ScoreLayout::Span inside{start, stop};
        for (size_t j = i > 0 ? i - 1 : 0; j <= i + 1 && j < bars; ++j)
            for (const Extent& b : barlines[j]) {
                if (b.x1 <= start || b.x0 >= stop) continue;
                if (b.x0 + b.x1 < start + stop)
                    inside.left = std::max(inside.left, b.x1);
                else
                    inside.right = std::min(inside.right, b.x0);
            }
        if (inside.left >= inside.right) inside = {start, stop};
        layout.barInsides.push_back(inside);
    }

    // The pinned column: as wide as the widest staff's clef and key signature need.
    layout.pinRight = layout.barStarts.front() + 3 * layout.staffSpace;  // if no staff says otherwise
    bool found = false;
    for (const lunasvg::Element& staff : childElements(measures.front())) {
        if (!hasClass(staff, "staff")) continue;
        const double pin = staffPinRight(staff, layout.staffSpace);
        if (pin < 0) continue;
        layout.pinRight = found ? std::max(layout.pinRight, pin) : pin;
        found = true;
    }
    return layout;
}

namespace {

// A printed bar number as a whole number, if it is one ("12", not "12a" or "").
std::optional<int> wholeNumber(const std::string& s) {
    int n = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (s.empty() || ec != std::errc() || end != s.data() + s.size() || n < 0) return std::nullopt;
    return n;
}

}  // namespace

BarNumbers::BarNumbers(const std::vector<std::string>& printed, int count) {
    bool usable = count > 0 && int(printed.size()) == count;
    bool anyWhole = false;
    for (size_t i = 0; usable && i < printed.size(); ++i) {
        usable = !printed[i].empty();
        anyWhole = anyWhole || wholeNumber(printed[i]);
    }
    counted_ = !(usable && anyWhole);
    for (int i = 0; i < count; ++i) {
        labels_.push_back(counted_ ? std::to_string(i + 1) : printed[size_t(i)]);
        numbers_.push_back(wholeNumber(labels_.back()));
    }
}

BarNumbers BarNumbers::of(const ScoreLayout& layout, const std::vector<std::string>& measureNumbers) {
    std::vector<std::string> printed;
    for (size_t measure : layout.barMeasures)
        printed.push_back(measure < measureNumbers.size() ? measureNumbers[measure] : "");
    return BarNumbers(printed, int(layout.barStarts.size()));
}

std::optional<std::pair<int, int>> BarNumbers::find(int number) const {
    std::optional<std::pair<int, int>> found;
    for (int i = 0; i < count(); ++i)
        if (numbers_[size_t(i)] == number) {
            if (!found) found = std::pair{i, i};
            found->second = i;
        }
    return found;
}

std::string BarNumbers::describe() const {
    std::optional<int> first, last;
    for (const std::optional<int>& n : numbers_)
        if (n) {
            if (!first) first = n;
            last = n;
        }
    if (!first) return "it has no numbered bars";
    if (*first == *last) return "its only bar is " + std::to_string(*first);
    return "its bars run from " + std::to_string(*first) + " to " + std::to_string(*last);
}

void prepareForPaging(SvgDocument& doc, const ScoreLayout& layout) {
    doc.splitInto(".system");
    // The pinned column is drawn from the system's own pieces (labels, brace, opening barline) and
    // the first bar, which holds the opening clefs and key signatures.
    std::vector<size_t> column;
    bool firstBar = true;
    for (size_t i = 0; i < doc.pieceCount(); ++i) {
        const bool isBar = doc.pieceClass(i).rfind("measure", 0) == 0;
        if (!isBar || firstBar) column.push_back(i);
        if (isBar) firstBar = false;
    }
    doc.pinColumn(layout.pinRight, std::move(column));
}
