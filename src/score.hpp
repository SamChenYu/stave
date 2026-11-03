#pragma once
#include <optional>
#include <string>
#include <vector>

// Verovio's SVG units per staff space (the gap between two staff lines). Verovio's unit is half
// a staff space and defaults to 9; tests/score_test.cpp measures the rendered staff to check it.
constexpr double kStaffSpaceUnits = 18;

struct EngraveResult {
    std::string svg;    // set on success
    std::string error;  // set on failure
};

// Lays out a score with Verovio as one long system, with no line or page breaks, so it scrolls
// sideways, and returns it as SVG. Reads MusicXML (.musicxml, .xml, compressed .mxl), MEI and ABC.
// dataDir is Verovio's data folder, with its music fonts.
EngraveResult engraveScore(const std::string& path, const std::string& dataDir);

// Where Verovio's data folder normally is: verovio-data/, next to the executable.
std::string defaultVerovioDataDir();

class SvgDocument;

// Where the bars and staves are in an engraved score, in the SVG's units.
struct ScoreLayout {
    struct Staff {
        double top, bottom;          // its top and bottom lines
        double bandTop, bandBottom;  // the space it owns: halfway to its neighbours, or the page edge
    };
    std::vector<double> barStarts;  // where each bar's staff lines begin, left to right
    double end = 0;                 // where the last bar's staff lines end
    std::vector<Staff> staves;      // top to bottom
    double staffSpace = kStaffSpaceUnits;
};

// Reads the layout from Verovio's SVG: each staff is a <g class="staff"> whose first <path>s are its
// lines, inside a <g class="measure"> per bar. Returns nothing if the document isn't laid out that way.
std::optional<ScoreLayout> findScoreLayout(const SvgDocument&);
