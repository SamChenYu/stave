#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "score.hpp"

// Two versions of a score side by side (--compare): which bars of one are which bars of the other,
// and how the two halves are put on one screen.

// Bars of the old version matched to bars of the new, by printed number: a run of old bars and the
// run of as many new bars it became, like 19-949=18-948 (old bars 19 to 949 are new bars 18 to 948).
struct BarRun {
    int firstOld, lastOld;
    int firstNew, lastNew;
};

// Reads --pairs: runs separated by commas, each OLD=NEW where both are a bar ("7") or a range
// ("12-15") of the same length. Bars missing from every run were deleted (old) or inserted (new).
// Appends the runs to `runs`; on a mistake, returns false with the reason in `error`.
bool parsePairs(std::string_view value, std::vector<BarRun>& runs, std::string& error);

// For each bar of one version, counting from 0, the bar of the other to show beside it.
struct BarMap {
    std::vector<int> newToOld;  // one for each bar of the new version
    std::vector<int> oldToNew;  // one for each bar of the old version
};

// Matches the bars of two versions. With runs, a bar in one of them is matched to its partner;
// a bar in none (inserted or deleted) to the bar just after the partner of the last matched bar
// before it, which is where it would be. Without runs, bars are matched by their position. Returns
// "" if all is well, otherwise what's wrong with the first run that isn't: a bar either version
// doesn't have.
std::string mapBars(const std::vector<BarRun>& runs, const BarNumbers& before, const BarNumbers& after,
                    BarMap& map);

// The width in pixels of each half of the screen, and the gap between them, for a screen `width`
// pixels wide. For braille, `cell` is 2 (dots per character), so both halves are whole characters.
struct SplitWidths {
    int left, gap, right;
};
SplitWidths splitWidths(int width, int cell);

// The two halves next to each other, `gap` pixels apart, with a line of ink down the middle of the
// gap. Each pixel keeps its tone. The result is as tall as the taller half.
Canvas sideBySide(const Canvas& left, const Canvas& right, int gap);
