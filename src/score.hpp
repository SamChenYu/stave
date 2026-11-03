#pragma once
#include <string>

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
