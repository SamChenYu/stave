#include "marks.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>

const char* markOption(Tone t) {
    switch (t) {
        case Tone::Added: return "--added";
        case Tone::Changed: return "--changed";
        case Tone::Removed: return "--removed";
        case Tone::Plain: break;
    }
    return "";
}

std::string describeMark(const Mark& m) {
    std::string s = std::to_string(m.firstBar);
    if (m.lastBar != m.firstBar) s += "-" + std::to_string(m.lastBar);
    if (m.staff > 0) s += "@" + std::to_string(m.staff);
    return s;
}

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

// A whole number from `least`, and nothing else.
bool readNumber(std::string_view s, int least, int& out) {
    s = trim(s);
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && ec == std::errc() && end == s.data() + s.size() && out >= least;
}

}  // namespace

bool parseMarks(std::string_view value, Tone tone, std::vector<Mark>& marks, std::string& error) {
    const std::string option = markOption(tone);
    const std::string example = " (like 7, 12-15 or 12-15@2)";
    std::vector<Mark> found;
    for (size_t start = 0; start <= value.size();) {
        const size_t comma = std::min(value.find(',', start), value.size());
        const std::string_view item = trim(value.substr(start, comma - start));
        start = comma + 1;
        if (item.empty()) {
            error = option + " needs bars" + example + ", not '" + std::string(value) + "'";
            return false;
        }
        const std::string bad = option + ": '" + std::string(item) + "' isn't";

        Mark m;
        m.tone = tone;
        std::string_view bars = item;
        if (const size_t at = item.find('@'); at != std::string_view::npos) {
            bars = item.substr(0, at);
            if (!readNumber(item.substr(at + 1), 1, m.staff)) {
                error = bad + " a bar on a staff" + example + "; staves count from 1";
                return false;
            }
        }
        const size_t dash = bars.find('-');
        bool ok;
        if (dash == std::string_view::npos) {
            ok = readNumber(bars, 0, m.firstBar);
            m.lastBar = m.firstBar;
        } else {
            ok = readNumber(bars.substr(0, dash), 0, m.firstBar) && readNumber(bars.substr(dash + 1), 0, m.lastBar);
        }
        if (!ok) {
            error = bad + " a bar or a range of bars" + example;
            return false;
        }
        if (m.lastBar < m.firstBar) {
            error = bad + " a range: it ends before it starts";
            return false;
        }
        found.push_back(m);
    }
    marks.insert(marks.end(), found.begin(), found.end());
    return true;
}

std::string toneName(Tone t) { return markOption(t) + 2; }

std::string nameMark(const Mark& m) {
    if (!m.from.empty()) return m.from + " (" + toneName(m.tone) + " " + describeMark(m) + ")";
    return std::string(markOption(m.tone)) + " " + describeMark(m);
}

namespace {

std::string named(const Mark& m) { return nameMark(m); }

}  // namespace

std::string checkMarks(std::vector<Mark>& marks, const BarNumbers& numbers, int staves) {
    for (Mark& m : marks) {
        m.firstIndex = m.lastIndex = -1;
        for (int bar : {m.firstBar, m.lastBar})
            if (!numbers.find(bar))
                return named(m) + ": the score has no bar " + std::to_string(bar) + "; " + numbers.describe();
        if (m.staff > staves)
            return named(m) + ": the score has only " + std::to_string(staves) + (staves == 1 ? " staff" : " staves");
        // A number can be on more than one bar (a bar split in two, say): a mark covers all of them.
        const int first = numbers.find(m.firstBar)->first, last = numbers.find(m.lastBar)->second;
        if (last < first)
            return named(m) + ": bar " + std::to_string(m.lastBar) + " comes before bar " + std::to_string(m.firstBar) +
                   " in this score";
        m.firstIndex = first;
        m.lastIndex = last;
    }
    const int bars = numbers.count();
    // Which mark has claimed each bar of each staff so far, so that a bar marked twice in
    // different tones is caught. Going cell by cell keeps a long list of marks quick to check.
    std::vector<int> owner(size_t(bars) * size_t(staves), -1);
    for (size_t i = 0; i < marks.size(); ++i) {
        const Mark& m = marks[i];
        const int firstStaff = m.staff > 0 ? m.staff : 1, lastStaff = m.staff > 0 ? m.staff : staves;
        for (int bar = m.firstIndex; bar <= m.lastIndex; ++bar)
            for (int staff = firstStaff; staff <= lastStaff; ++staff) {
                int& o = owner[size_t(bar) * size_t(staves) + size_t(staff - 1)];
                if (o >= 0 && marks[size_t(o)].tone != m.tone)
                    return named(m) + " overlaps " + named(marks[size_t(o)]) + ": a bar can't be both " +
                           toneName(marks[size_t(o)].tone) + " and " + toneName(m.tone);
                o = int(i);
            }
    }
    return "";
}

std::vector<SvgDocument::ToneRegion> toneRegions(const std::vector<Mark>& marks, const ScoreLayout& layout,
                                                 double pageWidth, double pageHeight) {
    const int bars = int(layout.barStarts.size()), staves = int(layout.staves.size());
    const bool insidesKnown = layout.barInsides.size() == layout.barStarts.size();
    std::vector<SvgDocument::ToneRegion> regions;
    for (const Mark& m : marks) {
        if (m.firstIndex < 0 || m.firstIndex > m.lastIndex || m.lastIndex >= bars || m.staff < 0 || m.staff > staves)
            continue;
        double y0 = 0, y1 = pageHeight;
        if (m.staff > 0) {
            y0 = layout.staves[size_t(m.staff - 1)].bandTop;
            y1 = layout.staves[size_t(m.staff - 1)].bandBottom;
        }
        // One region a bar, so that the barlines between them stay plain too.
        for (size_t b = size_t(m.firstIndex); b <= size_t(m.lastIndex); ++b) {
            if (insidesKnown)
                regions.push_back({layout.barInsides[b].left, y0, layout.barInsides[b].right, y1, m.tone});
            else
                regions.push_back(
                    {layout.barStarts[b], y0, b + 1 < size_t(bars) ? layout.barStarts[b + 1] : pageWidth, y1, m.tone});
        }
    }
    return regions;
}
