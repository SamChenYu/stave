#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "color.hpp"
#include "score.hpp"
#include "svgimage.hpp"

// Bars of a score marked as part of a diff, from --added, --changed and --removed: bars firstBar to
// lastBar (their printed numbers), on one staff (counting from 1, top to bottom), or on all of them.
struct Mark {
    Tone tone = Tone::Added;
    int firstBar = 1, lastBar = 1;
    int staff = 0;  // 0: every staff
    bool fill = false;  // tint the bars' background in the tone, rather than colour their ink
    // The bars it covers in the score's order, counting from 0 (BarNumbers' indices); found by
    // checkMarks, and -1 until then.
    int firstIndex = -1, lastIndex = -1;
    // Where it came from, for messages, when that's not an option: "diff.json, mark 3".
    std::string from;
};

// How a mark is named in messages: "--added 12-15@2", or "diff.json, mark 3 (added 12-15@2)".
std::string nameMark(const Mark&);

// "added", "changed" or "removed".
std::string toneName(Tone);

// The option that marks bars in this tone: "--added", "--changed" or "--removed".
const char* markOption(Tone);

// How a mark is written in an option's value: "7", "12-15", "12-15@2".
std::string describeMark(const Mark&);

// Reads one option's value: bars separated by commas, each a bar ("7", or often "0" for a pickup) or
// a range ("12-15"), optionally on one staff ("12-15@2"). Spaces around the parts are allowed. Appends the marks to
// `marks`; on a mistake, returns false with the reason in `error` (and appends nothing).
bool parseMarks(std::string_view value, Tone, std::vector<Mark>& marks, std::string& error);

// Finds each mark's bars in a score with these bar numbers and `staves` staves, setting its
// firstIndex and lastIndex, and checks that no bar of a staff is marked in two different tones (the
// same tone twice is fine). Returns "" if all is well, otherwise what's wrong with the first mark
// that isn't: a bar the score doesn't have, a staff past its last, or an overlap.
std::string checkMarks(std::vector<Mark>&, const BarNumbers&, int staves);

// Where the marks are on the page, in the score's units: a region for each bar of each mark that
// checkMarks has found, in order. A bar covers what's between its barlines (ScoreLayout::barInsides),
// so barlines, which neighbouring bars share, stay plain; without those, from where its staff lines
// start to where the next bar's do (the last one to the page's right edge). A staff covers its band (see
// ScoreLayout::Staff), and every staff the whole page's height. Marks not found are left out.
std::vector<SvgDocument::ToneRegion> toneRegions(const std::vector<Mark>&, const ScoreLayout&, double pageWidth,
                                                 double pageHeight);
