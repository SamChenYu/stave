#include <doctest/doctest.h>

#include <ostream>  // lets doctest print std::string
#include <string>
#include <utility>
#include <vector>

#include "notemarks.hpp"

namespace {

// Reads a marks file that should be fine.
std::pair<std::vector<Mark>, std::vector<NoteMark>> read(const std::string& json) {
    std::vector<Mark> bars;
    std::vector<NoteMark> notes;
    std::string error;
    REQUIRE_MESSAGE(parseMarksFile(json, "diff.json", bars, notes, error), error);
    return {bars, notes};
}

// The reason a marks file is refused, having checked it adds nothing.
std::string refused(const std::string& json) {
    std::vector<Mark> bars;
    std::vector<NoteMark> notes;
    std::string error;
    CHECK(!parseMarksFile(json, "diff.json", bars, notes, error));
    CHECK(bars.empty());
    CHECK(notes.empty());
    return error;
}

ScoreNote note(const std::string& id, size_t measure, int staff, int voice, int num, int den, int midi,
               const std::string& chord = "") {
    ScoreNote n;
    n.id = id;
    n.chord = chord;
    n.measure = measure;
    n.staff = staff;
    n.voice = voice;
    n.onsetNum = num;
    n.onsetDen = den;
    n.midi = midi;
    return n;
}

// Three bars on two staves. Bar 1: a D major chord above and a D below; bar 2: an A and a B a
// half beat later above, nothing below; bar 3: empty.
struct Score {
    ScoreLayout layout;
    BarNumbers numbers{{}, 3};
    std::vector<ScoreNote> notes;
    Score() {
        layout.barStarts = {100, 200, 300};
        layout.barMeasures = {0, 1, 2};
        layout.staves = {{50, 122, 0, 180}, {238, 310, 180, 400}};
        notes = {note("d", 0, 1, 1, 0, 1, 62, "chord"), note("fs", 0, 1, 1, 0, 1, 66, "chord"),
                 note("a", 0, 1, 1, 0, 1, 69, "chord"), note("low", 0, 2, 2, 0, 1, 50),
                 note("a2", 1, 1, 1, 0, 1, 69),          note("b2", 1, 1, 1, 1, 2, 71)};
    }

    // What findNoteMarks makes of a marks file, with `elements` set; "" if all's well.
    std::string find(const std::string& json, std::vector<std::pair<std::string, Tone>>& elements) {
        auto [bars, noteMarks] = read(json);
        REQUIRE(checkMarks(bars, numbers, 2) == "");
        return findNoteMarks(noteMarks, notes, layout, numbers, bars, elements);
    }
    std::vector<std::pair<std::string, Tone>> found(const std::string& json) {
        std::vector<std::pair<std::string, Tone>> elements;
        const std::string error = find(json, elements);
        CHECK_MESSAGE(error == "", error);
        return elements;
    }
    std::string error(const std::string& json) {
        std::vector<std::pair<std::string, Tone>> elements;
        return find(json, elements);
    }
};

using Elements = std::vector<std::pair<std::string, Tone>>;

}  // namespace

TEST_SUITE("note marks") {
    TEST_CASE("times are whole numbers, fractions or decimals, from the barline") {
        CHECK(parseTime("2") == BarTime{2, 1});
        CHECK(parseTime(" 3/2 ") == BarTime{3, 2});
        CHECK(parseTime("6/4") == BarTime{3, 2});
        CHECK(parseTime("1.5") == BarTime{3, 2});
        CHECK(parseTime(".25") == BarTime{1, 4});
        CHECK(parseTime("0") == BarTime{0, 1});
        for (const char* bad : {"", "x", "1/0", "-1", "1.", "1/2/3", "/2", "1.5.2"}) {
            CAPTURE(bad);
            CHECK(!parseTime(bad));
        }
        CHECK(describeTime({6, 4}) == "3/2");
        CHECK(describeTime({4, 2}) == "2");
    }

    TEST_CASE("a marks file has marks of whole bars and of notes") {
        const auto [bars, notes] = read(R"([
            {"tone": "added", "bar": 12, "to": 15},
            {"tone": "removed", "bar": 0, "staff": 2},
            {"tone": "changed", "bar": 12, "staff": 2, "at": "3/2", "pitch": 66},
            {"tone": "changed", "bar": 3, "at": 1.5, "voice": 2},
            {"tone": "added", "bar": 4, "at": "1/3"}
        ])");
        REQUIRE(bars.size() == 2);
        CHECK(bars[0].firstBar == 12);
        CHECK(bars[0].lastBar == 15);
        CHECK(bars[0].staff == 0);
        CHECK(nameMark(bars[1]) == "diff.json, mark 2 (removed 0@2)");
        REQUIRE(notes.size() == 3);
        CHECK(notes[0].at == BarTime{3, 2});
        CHECK(notes[0].pitch == 66);
        CHECK(nameNoteMark(notes[0]) == "diff.json, mark 3 (changed bar 12, staff 2, at 3/2, pitch 66)");
        CHECK(notes[1].at == BarTime{3, 2});
        CHECK(notes[1].voice == 2);
        CHECK(notes[2].at == BarTime{1, 3});
        CHECK(read("[]").first.empty());
    }

    TEST_CASE("mistakes in a marks file say which mark, and add nothing") {
        CHECK(refused("").find("diff.json: isn't a list of marks") == 0);
        CHECK(refused("{\"tone\": \"added\"}").find("isn't a list") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1},").find("isn't a list") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1}, 7]") == "diff.json, mark 2: isn't an object, like "
                                                                   "{\"tone\": \"added\", \"bar\": 12}");
        CHECK(refused("[{\"bar\": 1}]") == "diff.json, mark 1: needs a \"tone\": added, changed or removed");
        CHECK(refused("[{\"tone\": \"new\", \"bar\": 1}]").find("not \"new\"") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\"}]").find("needs a \"bar\"") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1.5}]").find("\"bar\" needs a whole number") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": \"1\"}]").find("\"bar\" needs a whole number") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1, \"staff\": 0}]").find("\"staff\"") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1, \"pitch\": 128}]").find("\"pitch\"") != std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1, \"beat\": 2}]").find("unknown field \"beat\"") !=
              std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1, \"at\": \"x\"}]").find("\"at\" needs a time") !=
              std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 1, \"to\": 3, \"at\": 0}]").find("\"to\" is for whole bars") !=
              std::string::npos);
        CHECK(refused("[{\"tone\": \"added\", \"bar\": 5, \"to\": 3}]").find("\"to\" is before \"bar\"") !=
              std::string::npos);
    }

    TEST_CASE("a mark finds the notes it describes") {
        Score s;
        // One note of the chord, by its time and pitch.
        CHECK(s.found(R"([{"tone": "changed", "bar": 1, "at": 0, "pitch": 66}])") == Elements{{"fs", Tone::Changed}});
        // Every note starting then, on one staff or all of them.
        CHECK(s.found(R"([{"tone": "added", "bar": 2, "staff": 1, "at": "1/2"}])") == Elements{{"b2", Tone::Added}});
        CHECK(s.found(R"([{"tone": "removed", "bar": 1, "voice": 2}])") == Elements{{"low", Tone::Removed}});
    }

    TEST_CASE("a chord whose notes are all marked alike is marked whole, with its stem and dots") {
        Score s;
        CHECK(s.found(R"([{"tone": "changed", "bar": 1, "staff": 1, "at": 0}])") == Elements{{"chord", Tone::Changed}});
        CHECK(s.found(R"([{"tone": "changed", "bar": 1, "pitch": 62}, {"tone": "changed", "bar": 1, "pitch": 66},
                          {"tone": "changed", "bar": 1, "pitch": 69}])") == Elements{{"chord", Tone::Changed}});
        // ...but not when they're marked differently, or some aren't.
        CHECK(s.found(R"([{"tone": "changed", "bar": 1, "pitch": 62}, {"tone": "added", "bar": 1, "pitch": 66}])") ==
              Elements{{"d", Tone::Changed}, {"fs", Tone::Added}});
    }

    TEST_CASE("a mark that finds no note says what's there") {
        Score s;
        CHECK(s.error(R"([{"tone": "changed", "bar": 1, "staff": 1, "at": "1/4"}])") ==
              "diff.json, mark 1 (changed bar 1, staff 1, at 1/4): there's no such note; the notes there start at 0");
        CHECK(s.error(R"([{"tone": "changed", "bar": 1, "at": 0, "pitch": 70}])") ==
              "diff.json, mark 1 (changed bar 1, at 0, pitch 70): there's no such note; the notes starting then "
              "sound 50, 62, 66, 69");
        CHECK(s.error(R"([{"tone": "changed", "bar": 2, "staff": 2, "at": 0}])") ==
              "diff.json, mark 1 (changed bar 2, staff 2, at 0): there's no such note: there are no notes there");
        CHECK(s.error(R"([{"tone": "changed", "bar": 9, "at": 0}])") ==
              "diff.json, mark 1 (changed bar 9, at 0): the score has no bar 9; its bars run from 1 to 3");
        CHECK(s.error(R"([{"tone": "changed", "bar": 1, "staff": 3, "at": 0}])").find("only 2 staves") !=
              std::string::npos);
    }

    TEST_CASE("changes start where each run of marked bars does") {
        std::vector<Mark> bars;
        std::vector<NoteMark> notes;
        std::string error;
        REQUIRE(parseMarksFile(R"([{"tone": "added", "bar": 3, "to": 5}, {"tone": "changed", "bar": 6, "staff": 2},
                                    {"tone": "removed", "bar": 9, "staff": 1}, {"tone": "changed", "bar": 9, "staff": 2,
                                    "at": 0}, {"tone": "added", "bar": 12, "staff": 2, "at": 1}])",
                               "diff.json", bars, notes, error));
        const BarNumbers numbers({}, 20);
        REQUIRE(checkMarks(bars, numbers, 2) == "");
        // Bars 3 to 6 run together; bar 9 has marks on two staves; bar 12 on staff 2.
        CHECK(changeStarts(bars, notes, numbers) == std::vector<std::pair<int, int>>{{2, 0}, {8, 0}, {11, 2}});
    }

    TEST_CASE("a note can't be marked two ways, nor differently from its bar") {
        Score s;
        CHECK(s.error(R"([{"tone": "changed", "bar": 1, "pitch": 66}, {"tone": "added", "bar": 1, "at": 0}])") ==
              "diff.json, mark 2 (added bar 1, at 0) overlaps diff.json, mark 1 (changed bar 1, pitch 66): a note "
              "can't be both changed and added");
        CHECK(s.error(R"([{"tone": "changed", "bar": 1, "staff": 2}, {"tone": "added", "bar": 1, "pitch": 50}])") ==
              "diff.json, mark 2 (added bar 1, pitch 50) is in a bar marked by diff.json, mark 1 (changed 1@2): a "
              "note can't be both changed and added");
        // The same way twice, or in a bar marked the same way, is fine.
        CHECK(s.found(R"([{"tone": "added", "bar": 1, "pitch": 66}, {"tone": "added", "bar": 1, "at": 0, "staff": 2},
                          {"tone": "added", "bar": 1, "staff": 2}])")
                  .size() == 2);
    }
}

TEST_CASE("a whole-bar mark can fill the bars' background instead of colouring their ink") {
    const auto [bars, notes] = read(R"([{"tone": "removed", "bar": 3, "to": 5, "fill": true}, {"tone": "added", "bar": 6}])");
    REQUIRE(bars.size() == 2);
    CHECK(bars[0].fill);
    CHECK(bars[0].lastBar == 5);
    CHECK_FALSE(bars[1].fill);
    CHECK(refused(R"([{"tone": "added", "bar": 3, "pitch": 60, "fill": true}])") ==
          "diff.json, mark 1: \"fill\" is for whole bars, but this marks notes");
    CHECK(refused(R"([{"tone": "added", "bar": 3, "fill": "yes"}])") == "diff.json, mark 1: \"fill\" is true or false");
}
