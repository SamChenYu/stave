#include "compare.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <system_error>

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

bool readNumber(std::string_view s, int& out) {
    s = trim(s);
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && ec == std::errc() && end == s.data() + s.size() && out >= 0;
}

// "7" or "12-15": first and last bar.
bool readRange(std::string_view s, int& first, int& last) {
    s = trim(s);
    if (const size_t dash = s.find('-'); dash != std::string_view::npos)
        return readNumber(s.substr(0, dash), first) && readNumber(s.substr(dash + 1), last) && first <= last;
    if (!readNumber(s, first)) return false;
    last = first;
    return true;
}

// Fills the bars no run reaches: each takes the bar just after the partner of the matched bar
// before it (or the first bar, if none is before it).
void fillGaps(std::vector<int>& map, int otherCount) {
    int next = 0;  // where an unmatched bar goes: just after the last partner seen
    for (int& m : map) {
        if (m >= 0)
            next = m + 1;
        else
            m = std::clamp(next, 0, std::max(0, otherCount - 1));
    }
}

}  // namespace

bool parsePairs(std::string_view value, std::vector<BarRun>& runs, std::string& error) {
    const std::string example = " (like 1-17=1-17,19-40=18-39)";
    std::vector<BarRun> found;
    for (size_t start = 0; start <= value.size();) {
        const size_t comma = std::min(value.find(',', start), value.size());
        const std::string_view item = trim(value.substr(start, comma - start));
        start = comma + 1;
        const size_t equals = item.find('=');
        BarRun r{};
        if (item.empty() || equals == std::string_view::npos ||
            !readRange(item.substr(0, equals), r.firstOld, r.lastOld) ||
            !readRange(item.substr(equals + 1), r.firstNew, r.lastNew)) {
            error = "--pairs: '" + std::string(item) + "' isn't a run of old bars = new bars" + example;
            return false;
        }
        if (r.lastOld - r.firstOld != r.lastNew - r.firstNew) {
            error = "--pairs: '" + std::string(item) + "' matches different numbers of bars";
            return false;
        }
        found.push_back(r);
    }
    runs.insert(runs.end(), found.begin(), found.end());
    return true;
}

std::string mapBars(const std::vector<BarRun>& runs, const BarNumbers& before, const BarNumbers& after,
                    BarMap& map) {
    map.newToOld.assign(size_t(after.count()), -1);
    map.oldToNew.assign(size_t(before.count()), -1);

    if (runs.empty()) {
        for (int i = 0; i < std::min(before.count(), after.count()); ++i) {
            map.newToOld[size_t(i)] = i;
            map.oldToNew[size_t(i)] = i;
        }
    }
    for (const BarRun& r : runs) {
        for (int k = 0; k <= r.lastOld - r.firstOld; ++k) {
            const std::optional<std::pair<int, int>> o = before.find(r.firstOld + k);
            const std::optional<std::pair<int, int>> n = after.find(r.firstNew + k);
            if (!o) return "--pairs: the old version has no bar " + std::to_string(r.firstOld + k);
            if (!n) return "--pairs: the new version has no bar " + std::to_string(r.firstNew + k);
            map.oldToNew[size_t(o->first)] = n->first;
            map.newToOld[size_t(n->first)] = o->first;
        }
    }
    fillGaps(map.newToOld, before.count());
    fillGaps(map.oldToNew, after.count());
    return "";
}

SplitWidths splitWidths(int width, int cell) {
    cell = std::max(1, cell);
    const int cells = std::max(3, width / cell);
    const int left = (cells - 1) / 2;
    return {left * cell, cell, (cells - 1 - left) * cell};
}

Canvas sideBySide(const Canvas& left, const Canvas& right, int gap) {
    Canvas out(left.w + gap + right.w, std::max(left.h, right.h));
    const bool toned = !left.tone.empty() || !right.tone.empty();
    if (toned) out.tone.assign(out.px.size(), uint8_t(Tone::Plain));

    const auto copy = [&](const Canvas& c, int x0) {
        for (int y = 0; y < c.h; ++y) {
            for (int x = 0; x < c.w; ++x) {
                out.at(x0 + x, y) = c.at(x, y);
                if (toned) out.tone[size_t(y) * out.w + x0 + x] = uint8_t(c.toneAt(x, y));
            }
        }
    };
    copy(left, 0);
    copy(right, left.w + gap);

    // A line down the middle of the gap, at least one pixel wide.
    const int lineW = std::max(1, gap / 4), lineX = left.w + (gap - lineW) / 2;
    for (int y = 0; y < out.h; ++y)
        for (int x = lineX; x < lineX + lineW; ++x) out.at(x, y) = 255;
    return out;
}
