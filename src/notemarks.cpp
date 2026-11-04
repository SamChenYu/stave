#include "notemarks.hpp"

#include <jsonxx.h>  // from Verovio

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <system_error>

namespace {

BarTime reduced(long long num, long long den) {
    const long long g = std::gcd(num, den);
    return {int(num / (g ? g : 1)), int(den / (g ? g : 1))};
}

bool earlier(BarTime a, BarTime b) { return (long long)a.num * b.den < (long long)b.num * a.den; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

// A whole number from 0, and nothing else.
std::optional<long long> readWhole(std::string_view s) {
    long long n = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (s.empty() || ec != std::errc() || end != s.data() + s.size() || n < 0) return std::nullopt;
    return n;
}

}  // namespace

bool operator==(BarTime a, BarTime b) { return (long long)a.num * b.den == (long long)b.num * a.den; }

std::string describeTime(BarTime t) {
    const BarTime r = reduced(t.num, t.den);
    return r.den == 1 ? std::to_string(r.num) : std::to_string(r.num) + "/" + std::to_string(r.den);
}

std::optional<BarTime> parseTime(std::string_view s) {
    s = trim(s);
    if (const size_t slash = s.find('/'); slash != std::string_view::npos) {
        const auto num = readWhole(trim(s.substr(0, slash))), den = readWhole(trim(s.substr(slash + 1)));
        if (!num || !den || *den == 0 || *num > 1'000'000 || *den > 1'000'000) return std::nullopt;
        return reduced(*num, *den);
    }
    if (const size_t point = s.find('.'); point != std::string_view::npos) {
        const std::string_view whole = s.substr(0, point), digits = s.substr(point + 1);
        if (digits.empty() || digits.size() > 6) return std::nullopt;
        const auto w = whole.empty() ? std::optional<long long>(0) : readWhole(whole);
        const auto f = readWhole(digits);
        if (!w || !f || *w > 1'000'000) return std::nullopt;
        long long den = 1;
        for (size_t i = 0; i < digits.size(); ++i) den *= 10;
        return reduced(*w * den + *f, den);
    }
    const auto n = readWhole(s);
    if (!n || *n > 1'000'000) return std::nullopt;
    return BarTime{int(*n), 1};
}

std::string nameNoteMark(const NoteMark& m) {
    std::string s = toneName(m.tone) + " bar " + std::to_string(m.bar);
    if (m.staff > 0) s += ", staff " + std::to_string(m.staff);
    if (m.at) s += ", at " + describeTime(*m.at);
    if (m.pitch) s += ", pitch " + std::to_string(*m.pitch);
    if (m.voice) s += ", voice " + std::to_string(*m.voice);
    return m.from.empty() ? s : m.from + " (" + s + ")";
}

bool parseMarksFile(std::string_view json, const std::string& name, std::vector<Mark>& bars,
                    std::vector<NoteMark>& notes, std::string& error) {
    jsonxx::Array list;
    const std::string text(trim(json));
    if (text.empty() || text.front() != '[' || !list.parse(text)) {
        error = name + ": isn't a list of marks (a JSON array, like [{\"tone\": \"added\", \"bar\": 12}])";
        return false;
    }
    std::vector<Mark> foundBars;
    std::vector<NoteMark> foundNotes;
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string from = name + ", mark " + std::to_string(i + 1);
        auto fail = [&](const std::string& why) {
            error = from + ": " + why;
            return false;
        };
        if (!list.values()[i]->is<jsonxx::Object>())
            return fail("isn't an object, like {\"tone\": \"added\", \"bar\": 12}");
        const jsonxx::Object& o = list.get<jsonxx::Object>(unsigned(i));
        static const std::set<std::string> kKnown{"tone", "bar", "to", "staff", "at", "pitch", "voice", "fill"};
        for (const auto& field : o.kv_map())
            if (!kKnown.count(field.first))
                return fail("unknown field \"" + field.first +
                            "\" (marks have tone, bar, to, staff, at, pitch, voice and fill)");
        if (o.has<jsonxx::Value>("fill") && !o.has<jsonxx::Boolean>("fill"))
            return fail("\"fill\" is true or false");
        const bool fill = o.has<jsonxx::Boolean>("fill") && o.get<jsonxx::Boolean>("fill");
        // A field that must be a whole number from `least`, if it's there.
        auto whole = [&](const char* key, long long least, long long most, std::optional<int>& out) {
            if (!o.has<jsonxx::Value>(key)) return true;
            const bool isNumber = o.has<jsonxx::Number>(key);
            const long double v = isNumber ? o.get<jsonxx::Number>(key) : 0;
            if (!isNumber || v != std::floor(v) || v < least || v > most)
                return fail(std::string("\"") + key + "\" needs a whole number from " + std::to_string(least));
            out = int(v);
            return true;
        };

        NoteMark m;
        m.from = from;
        if (!o.has<jsonxx::String>("tone")) return fail("needs a \"tone\": added, changed or removed");
        const std::string& tone = o.get<jsonxx::String>("tone");
        if (tone == "added") m.tone = Tone::Added;
        else if (tone == "changed") m.tone = Tone::Changed;
        else if (tone == "removed") m.tone = Tone::Removed;
        else return fail("\"tone\" is added, changed or removed, not \"" + tone + "\"");

        std::optional<int> bar, to, staff;
        if (!o.has<jsonxx::Value>("bar")) return fail("needs a \"bar\": its printed number");
        if (!whole("bar", 0, 1'000'000, bar) || !whole("to", 0, 1'000'000, to) || !whole("staff", 1, 1'000, staff) ||
            !whole("pitch", 0, 127, m.pitch) || !whole("voice", 1, 1'000, m.voice))
            return false;
        m.bar = *bar;
        m.staff = staff.value_or(0);
        if (o.has<jsonxx::Value>("at")) {
            if (o.has<jsonxx::String>("at")) {
                m.at = parseTime(o.get<jsonxx::String>("at"));
            } else if (o.has<jsonxx::Number>("at")) {
                // A number, like 1.5: as a fraction with a small denominator, if it's one.
                const long double v = o.get<jsonxx::Number>("at");
                for (int den = 1; den <= 960 && !m.at && v >= 0; ++den)
                    if (std::fabs(double(v * den - std::round(v * den))) < 1e-9)
                        m.at = reduced((long long)std::llround(double(v * den)), den);
            }
            if (!m.at) return fail("\"at\" needs a time from the barline in quarter notes, like 2, 1.5 or \"1/3\"");
        }

        if (m.at || m.pitch || m.voice) {
            if (to) return fail("\"to\" is for whole bars, but this marks notes, which are in one bar");
            if (fill) return fail("\"fill\" is for whole bars, but this marks notes");
            foundNotes.push_back(std::move(m));
        } else {
            if (to && *to < *bar) return fail("\"to\" is before \"bar\"");
            Mark b;
            b.tone = m.tone;
            b.firstBar = *bar;
            b.lastBar = to.value_or(*bar);
            b.staff = m.staff;
            b.fill = fill;
            b.from = from;
            foundBars.push_back(std::move(b));
        }
    }
    bars.insert(bars.end(), foundBars.begin(), foundBars.end());
    notes.insert(notes.end(), foundNotes.begin(), foundNotes.end());
    return true;
}

std::vector<std::pair<int, int>> changeStarts(const std::vector<Mark>& bars, const std::vector<NoteMark>& notes,
                                              const BarNumbers& numbers) {
    std::map<int, int> staffOf;  // each marked bar: the staff it's marked on, or 0 for more than one
    auto add = [&](int bar, int staff) {
        const auto [it, added] = staffOf.emplace(bar, staff);
        if (!added && it->second != staff) it->second = 0;
    };
    for (const Mark& m : bars)
        for (int b = m.firstIndex; m.firstIndex >= 0 && b <= m.lastIndex; ++b) add(b, m.staff);
    for (const NoteMark& m : notes)
        if (const auto range = numbers.find(m.bar))
            for (int b = range->first; b <= range->second; ++b) add(b, m.staff);
    std::vector<std::pair<int, int>> starts;
    for (const auto& [bar, staff] : staffOf)
        if (!staffOf.count(bar - 1)) starts.emplace_back(bar, staff);
    return starts;
}

std::string findNoteMarks(const std::vector<NoteMark>& marks, const std::vector<ScoreNote>& notes,
                          const ScoreLayout& layout, const BarNumbers& numbers, const std::vector<Mark>& bars,
                          std::vector<std::pair<std::string, Tone>>& elements) {
    elements.clear();
    const int staves = int(layout.staves.size()), barCount = numbers.count();
    // Which bar each <g class="measure"> is (most are one; a measure without staff lines isn't).
    std::vector<int> barOf;
    for (size_t b = 0; b < layout.barMeasures.size(); ++b) {
        if (barOf.size() <= layout.barMeasures[b]) barOf.resize(layout.barMeasures[b] + 1, -1);
        barOf[layout.barMeasures[b]] = int(b);
    }
    auto barOfNote = [&](const ScoreNote& n) { return n.measure < barOf.size() ? barOf[n.measure] : -1; };
    // Which bar mark covers each bar of each staff, to catch a note marked otherwise inside it.
    std::vector<int> barOwner(size_t(barCount) * size_t(std::max(staves, 0)), -1);
    for (size_t i = 0; i < bars.size(); ++i) {
        const Mark& m = bars[i];
        if (m.firstIndex < 0) continue;
        for (int b = m.firstIndex; b <= m.lastIndex && b < barCount; ++b)
            for (int s = m.staff > 0 ? m.staff : 1; s <= (m.staff > 0 ? m.staff : staves); ++s)
                barOwner[size_t(b) * size_t(staves) + size_t(s - 1)] = int(i);
    }

    std::map<std::string, std::pair<Tone, size_t>> marked;  // note id: its tone, and which mark gave it
    for (size_t i = 0; i < marks.size(); ++i) {
        const NoteMark& m = marks[i];
        const std::optional<std::pair<int, int>> range = numbers.find(m.bar);
        if (!range)
            return nameNoteMark(m) + ": the score has no bar " + std::to_string(m.bar) + "; " + numbers.describe();
        if (m.staff > staves)
            return nameNoteMark(m) + ": the score has only " + std::to_string(staves) + (staves == 1 ? " staff" : " staves");

        std::vector<const ScoreNote*> there, found;  // the bar's notes on the staff and voice; the ones that match
        for (const ScoreNote& n : notes) {
            const int b = barOfNote(n);
            if (b < range->first || b > range->second) continue;
            if ((m.staff > 0 && n.staff != m.staff) || (m.voice && n.voice != *m.voice)) continue;
            there.push_back(&n);
            if (m.at && !(BarTime{n.onsetNum, n.onsetDen} == *m.at)) continue;
            if (m.pitch && n.midi != *m.pitch) continue;
            found.push_back(&n);
        }
        if (found.empty()) {
            std::string why = "there's no such note";
            std::vector<const ScoreNote*> then;
            for (const ScoreNote* n : there)
                if (!m.at || BarTime{n->onsetNum, n->onsetDen} == *m.at) then.push_back(n);
            if (there.empty()) {
                why += ": there are no notes there";
            } else if (then.empty()) {
                std::vector<BarTime> starts;
                for (const ScoreNote* n : there) starts.push_back({n->onsetNum, n->onsetDen});
                std::sort(starts.begin(), starts.end(), earlier);
                starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
                why += "; the notes there start at";
                for (size_t k = 0; k < starts.size(); ++k) why += (k ? ", " : " ") + describeTime(starts[k]);
            } else if (m.pitch) {
                std::vector<int> pitches;
                for (const ScoreNote* n : then) pitches.push_back(n->midi);
                std::sort(pitches.begin(), pitches.end());
                pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
                why += std::string("; the notes ") + (m.at ? "starting then" : "there") + " sound";
                for (size_t k = 0; k < pitches.size(); ++k) why += (k ? ", " : " ") + std::to_string(pitches[k]);
            }
            return nameNoteMark(m) + ": " + why;
        }
        for (const ScoreNote* n : found) {
            if (const auto it = marked.find(n->id); it != marked.end() && it->second.first != m.tone)
                return nameNoteMark(m) + " overlaps " + nameNoteMark(marks[it->second.second]) +
                       ": a note can't be both " + toneName(it->second.first) + " and " + toneName(m.tone);
            const int b = barOfNote(*n);
            if (n->staff >= 1 && n->staff <= staves) {
                const int owner = barOwner[size_t(b) * size_t(staves) + size_t(n->staff - 1)];
                if (owner >= 0 && bars[size_t(owner)].tone != m.tone)
                    return nameNoteMark(m) + " is in a bar marked by " + nameMark(bars[size_t(owner)]) +
                           ": a note can't be both " + toneName(bars[size_t(owner)].tone) + " and " + toneName(m.tone);
            }
            marked.emplace(n->id, std::pair{m.tone, i});
        }
    }

    // A chord all of whose notes are marked alike is marked as a whole, taking its stem and dots.
    std::map<std::string, std::vector<const ScoreNote*>> chords;
    for (const ScoreNote& n : notes)
        if (!n.chord.empty()) chords[n.chord].push_back(&n);
    std::set<std::string> wholeChords;
    for (const ScoreNote& n : notes) {
        const auto it = marked.find(n.id);
        if (it == marked.end()) continue;
        const Tone tone = it->second.first;
        if (!n.chord.empty()) {
            if (wholeChords.count(n.chord)) continue;
            const std::vector<const ScoreNote*>& all = chords[n.chord];
            const bool alike = std::all_of(all.begin(), all.end(), [&](const ScoreNote* other) {
                const auto o = marked.find(other->id);
                return o != marked.end() && o->second.first == tone;
            });
            if (alike) {
                wholeChords.insert(n.chord);
                elements.emplace_back(n.chord, tone);
                continue;
            }
        }
        elements.emplace_back(n.id, tone);
    }
    return "";
}
