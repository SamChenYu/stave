#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Verovio's SVG units per staff space (the gap between two staff lines). Verovio's unit is half
// a staff space and defaults to 9; tests/score_test.cpp measures the rendered staff to check it.
constexpr double kStaffSpaceUnits = 18;

// A note of an engraved score, for finding notes the way motif describes them.
struct ScoreNote {
    std::string id;        // its <g class="note"> element's id in the SVG
    std::string chord;     // the id of the <g class="chord"> it's in, or "" if it isn't in one
    size_t measure = 0;    // which <g class="measure"> it's in, counting from 0 (see ScoreLayout::barMeasures)
    int staff = 1;         // the staff it belongs to, counting from 1, top to bottom
    int voice = 1;         // its voice (Verovio's layer) on that staff
    int onsetNum = 0, onsetDen = 1;  // when it starts: quarter notes from the barline, as a fraction
    int midi = 60;         // the pitch it sounds, as a MIDI number (transposing instruments included)
};

struct EngraveResult {
    std::string svg;    // set on success
    std::string error;  // set on failure
    // Each <g class="measure">'s printed number, as the file gives it ("12", "0" for a pickup, maybe
    // "12a"; "" if none), in the SVG's order. lunasvg drops attributes it doesn't know, so these
    // are read from the SVG's text (see measureNumbers).
    std::vector<std::string> measureNumbers;
    // With readNotes: every note, in Verovio's document order (bar by bar, then staff, voice and time).
    std::vector<ScoreNote> notes;
    // Each <g class="measure">'s label, from MEI's measure@label ("" if none), in the SVG's order: what
    // to call the bar on screen when its number is only there to find it by.
    std::vector<std::string> measureLabels;
};

// Lays out a score with Verovio as one long system, with no line or page breaks, so it scrolls
// sideways, and returns it as SVG. Reads MusicXML (.musicxml, .xml, compressed .mxl), MEI and ABC.
// dataDir is Verovio's data folder, with its music fonts. readNotes also lists the notes, which takes
// a few percent longer.
EngraveResult engraveScore(const std::string& path, const std::string& dataDir, bool readNotes = false);

// Where Verovio's data folder normally is: verovio-data/, next to the executable.
std::string defaultVerovioDataDir();

// The data-n attribute of each <g class="measure"> in Verovio's SVG (drawn with svgAdditionalAttribute
// measure@n), in order; "" where a measure has none.
std::vector<std::string> measureNumbers(std::string_view svg);

// The data-label attribute of each <g class="measure"> (measure@label), in order; "" where a measure has none.
std::vector<std::string> measureLabels(std::string_view svg);

class SvgDocument;

// Where the bars and staves are in an engraved score, in the SVG's units.
struct ScoreLayout {
    struct Staff {
        double top, bottom;          // its top and bottom lines
        double bandTop, bandBottom;  // the space it owns: halfway to its neighbours, or the page edge
    };
    std::vector<double> barStarts;  // where each bar's staff lines begin, left to right
    std::vector<size_t> barMeasures;  // which <g class="measure"> each bar is, counting from 0
    double end = 0;                 // where the last bar's staff lines end
    // Each bar between its barlines (which neighbouring bars share): from just right of the barline
    // it starts at, if any, to just left of the one it ends with. One for each of barStarts.
    struct Span {
        double left, right;
    };
    std::vector<Span> barInsides;
    std::vector<Staff> staves;      // top to bottom
    double staffSpace = kStaffSpaceUnits;
    // Where the column that stays put while the bars scroll ends: just after the opening clefs and
    // key signatures, before the time signature. Everything left of it (labels, brace, clefs, keys)
    // is drawn from the start of the system.
    double pinRight = 0;
};

// Reads the layout from Verovio's SVG: each staff is a <g class="staff"> whose first <path>s are its
// lines, inside a <g class="measure"> per bar. Returns nothing if the document isn't laid out that way.
std::optional<ScoreLayout> findScoreLayout(const SvgDocument&);

// The numbers printed on a score's bars, for finding bars by them as people (and motif) do. A pickup
// is usually bar 0. A bar whose number isn't a whole number (like "12a") can't be found by itself,
// but is within any range around it. Bars are counted from 0 in the score's order ("indices").
class BarNumbers {
public:
    // `count` bars with these printed numbers. Unless every bar has one, and at least one is a whole
    // number, bars are numbered by counting from 1 instead.
    BarNumbers(const std::vector<std::string>& printed, int count);
    // The bars of a layout, with the numbers of the measures they came from (EngraveResult::measureNumbers).
    static BarNumbers of(const ScoreLayout&, const std::vector<std::string>& measureNumbers);

    int count() const { return int(labels_.size()); }
    bool counted() const { return counted_; }  // numbered by counting, not as printed
    // The first and last bar numbered `number`, or nothing if none is.
    std::optional<std::pair<int, int>> find(int number) const;
    // Bar i's number, for showing: "12", or "12a".
    const std::string& label(int i) const { return labels_[size_t(i)]; }
    const std::vector<std::string>& labels() const { return labels_; }
    // Which numbers there are, for an error message: "its bars run from 0 to 84".
    std::string describe() const;

private:
    std::vector<std::string> labels_;
    std::vector<std::optional<int>> numbers_;  // each label as a whole number, if it is one
    bool counted_ = true;
};

// What the status line calls each bar: its measure's label (EngraveResult::measureLabels) where it has
// one, otherwise its number. A score can number its bars one way, to find them by, and show another.
std::vector<std::string> barLabels(const ScoreLayout&, const BarNumbers&, const std::vector<std::string>& measureLabels);

// Gets an engraved score ready for the pager: draws only the bars near the screen, so long scores
// scroll smoothly, and keeps the opening clefs and key signatures (up to layout.pinRight) at the
// left edge while the bars scroll past.
void prepareForPaging(SvgDocument&, const ScoreLayout&);
