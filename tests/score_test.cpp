#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "score.hpp"
#include "svgimage.hpp"

namespace {

std::string testData(const std::string& name) { return std::string(STAVE_TEST_DATA_DIR) + "/" + name; }

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST_SUITE("engraving") {
    TEST_CASE("a score becomes one long system of SVG") {
        const EngraveResult r = engraveScore(testData("scale.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto doc = SvgDocument::fromData(r.svg, error);
        REQUIRE_MESSAGE(doc, error);
        CHECK(doc->width() > 4 * doc->height());  // four bars side by side, not wrapped
    }

    TEST_CASE("a staff space is kStaffSpaceUnits units") {
        // Draw the scale at 2 pixels per unit and look down a column in the last bar, after the
        // whole note and before the final barline, where only the five staff lines are.
        const EngraveResult r = engraveScore(testData("scale.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto doc = SvgDocument::fromData(r.svg, error);
        REQUIRE(doc);
        const double scale = 2;
        const int w = int(doc->width() * scale), h = int(doc->height() * scale);
        const Canvas c = doc->render(scale, 0, 0, w, h);
        const int x = int(w * 0.9);

        std::vector<double> lines;  // the middle of each run of inked pixels
        for (int y = 0; y < h;) {
            if (c.at(x, y) < 128) {
                ++y;
                continue;
            }
            const int top = y;
            while (y < h && c.at(x, y) >= 128) ++y;
            lines.push_back((top + y - 1) / 2.0);
        }
        REQUIRE(lines.size() == 5);
        for (size_t i = 1; i < lines.size(); ++i)
            CHECK((lines[i] - lines[i - 1]) / scale == doctest::Approx(kStaffSpaceUnits).epsilon(0.05));
    }

    TEST_CASE("drawing only the bars near the window gives exactly the same pixels as drawing it all") {
        const EngraveResult r = engraveScore(testData("piano.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto whole = SvgDocument::fromData(r.svg, error);
        const auto split = SvgDocument::fromData(r.svg, error);
        REQUIRE(whole);
        REQUIRE(split);
        CHECK(split->splitInto(".system") >= 5);  // opening barline, brace, three bars
        struct Window {
            double scale, x, y;
            int w, h;
        };
        for (const Window win : {Window{0.75, 0, 0, 300, 200}, Window{0.75, 230, 40, 150, 120},
                                 Window{2, 900, 100, 400, 300}, Window{2, 1500, 0, 300, 600},
                                 Window{0.3, 0, 0, 400, 100}}) {
            CAPTURE(win.scale);
            CAPTURE(win.x);
            const Canvas a = whole->render(win.scale, win.x, win.y, win.w, win.h);
            const Canvas b = split->render(win.scale, win.x, win.y, win.w, win.h);
            int differ = 0;
            for (size_t i = 0; i < a.px.size(); ++i) differ += a.px[i] != b.px[i];
            CHECK(differ == 0);
        }
    }

    TEST_CASE("a document with nothing matching the selector is drawn whole as before") {
        std::string error;
        const auto doc = SvgDocument::fromFile(testData("staff-note.svg"), error);
        REQUIRE(doc);
        CHECK(doc->splitInto(".system") == 0);
        const Canvas c = doc->render(1, 0, 0, 200, 80);
        CHECK(c.at(100, 20) == 255);  // a staff line
    }

    TEST_CASE("finds the bars and staves of a grand staff") {
        const EngraveResult r = engraveScore(testData("piano.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto doc = SvgDocument::fromData(r.svg, error);
        REQUIRE(doc);
        const std::optional<ScoreLayout> layout = findScoreLayout(*doc);
        REQUIRE(layout);
        REQUIRE(layout->barStarts.size() == 3);
        CHECK(layout->barStarts[0] < layout->barStarts[1]);
        CHECK(layout->barStarts[1] < layout->barStarts[2]);
        CHECK(layout->end > layout->barStarts[2]);
        CHECK(layout->staffSpace == doctest::Approx(kStaffSpaceUnits).epsilon(0.05));
        REQUIRE(layout->staves.size() == 2);
        for (const ScoreLayout::Staff& staff : layout->staves)
            CHECK(staff.bottom - staff.top == doctest::Approx(4 * layout->staffSpace));  // five lines
        CHECK(layout->staves[0].bottom < layout->staves[1].top);
        // The bands cover the whole height without gaps or overlaps.
        CHECK(layout->staves[0].bandTop == 0);
        CHECK(layout->staves[0].bandBottom == layout->staves[1].bandTop);
        CHECK(layout->staves[1].bandBottom == doc->height());
    }

    TEST_CASE("each bar's inside stops short of the barlines on either side, but not by much") {
        const EngraveResult r = engraveScore(testData("piano.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto doc = SvgDocument::fromData(r.svg, error);
        REQUIRE(doc);
        const std::optional<ScoreLayout> layout = findScoreLayout(*doc);
        REQUIRE(layout);
        REQUIRE(layout->barInsides.size() == 3);
        const double space = layout->staffSpace;
        for (size_t i = 0; i < 3; ++i) {
            CAPTURE(i);
            const ScoreLayout::Span& in = layout->barInsides[i];
            const double stop = i + 1 < 3 ? layout->barStarts[i + 1] : layout->end;
            CHECK(in.left >= layout->barStarts[i]);
            CHECK(in.right < stop);  // each bar here ends with a barline
            CHECK(in.right > stop - 2 * space);  // the final barline is the widest, at under 2 spaces
            if (i > 0) CHECK(in.left > layout->barStarts[i]);
            if (i > 0) CHECK(in.left < layout->barStarts[i] + space);
        }
    }

    TEST_CASE("each bar's printed number comes from Verovio's SVG") {
        CHECK(measureNumbers("<g id=\"a\" class=\"measure\" data-n=\"0\"><g class=\"staff\"/></g>"
                             "<g class=\"mNum\"/><g id=\"b\" class=\"measure\"></g>"
                             "<g class=\"measure\" data-n=\"12a\"/>") == std::vector<std::string>{"0", "", "12a"});
        const EngraveResult r = engraveScore(testData("piano.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        CHECK(r.measureNumbers == std::vector<std::string>{"1", "2", "3"});
        std::string error;
        const auto doc = SvgDocument::fromData(r.svg, error);
        REQUIRE(doc);
        const std::optional<ScoreLayout> layout = findScoreLayout(*doc);
        REQUIRE(layout);
        CHECK(layout->barMeasures == std::vector<size_t>{0, 1, 2});
        const BarNumbers numbers = BarNumbers::of(*layout, r.measureNumbers);
        CHECK(!numbers.counted());
        CHECK(numbers.labels() == std::vector<std::string>{"1", "2", "3"});
    }

    TEST_CASE("every note, with its bar, staff, voice, start and pitch") {
        CHECK(engraveScore(testData("piano.musicxml"), defaultVerovioDataDir()).notes.empty());  // unless asked
        const EngraveResult r = engraveScore(testData("piano.musicxml"), defaultVerovioDataDir(), /*readNotes=*/true);
        REQUIRE_MESSAGE(r.error == "", r.error);
        // Written as (measure, staff, voice, start in quarter notes, MIDI pitch).
        std::vector<std::string> got;
        for (const ScoreNote& n : r.notes) {
            CHECK(!n.id.empty());
            got.push_back(std::to_string(n.measure) + " s" + std::to_string(n.staff) + " v" + std::to_string(n.voice) +
                          " " + std::to_string(n.onsetNum) + "/" + std::to_string(n.onsetDen) + " " +
                          std::to_string(n.midi));
        }
        const std::vector<std::string> want{
            // Bar 1: a D major chord, then a rest, over a dotted half note in the left hand.
            "0 s1 v1 0/1 62", "0 s1 v1 0/1 66", "0 s1 v1 0/1 69", "0 s2 v2 0/1 50",
            // Bar 2: four eighth notes and a quarter, over three quarters.
            "1 s1 v1 0/1 69", "1 s1 v1 1/2 71", "1 s1 v1 1/1 72", "1 s1 v1 3/2 71", "1 s1 v1 2/1 69",
            "1 s2 v2 0/1 54", "1 s2 v2 1/1 55", "1 s2 v2 2/1 45",
            // Bar 3: three quarters, over a two-note chord.
            "2 s1 v1 0/1 69", "2 s1 v1 1/1 66", "2 s1 v1 2/1 62", "2 s2 v2 0/1 50", "2 s2 v2 0/1 57"};
        CHECK(got == want);
        // The notes of a chord say which it is; others are in none.
        CHECK(!r.notes[0].chord.empty());
        CHECK(r.notes[0].chord == r.notes[2].chord);
        CHECK(r.notes[3].chord.empty());
        CHECK(r.notes[16].chord == r.notes[15].chord);
        CHECK(r.notes[16].chord != r.notes[0].chord);
    }

    TEST_CASE("a note's pitch is what it sounds, for a transposing instrument and under an 8va line") {
        const EngraveResult r = engraveScore(testData("clarinet.musicxml"), defaultVerovioDataDir(), /*readNotes=*/true);
        REQUIRE_MESSAGE(r.error == "", r.error);
        REQUIRE(r.notes.size() == 2);
        CHECK(r.notes[0].midi == 72);
        CHECK(r.notes[1].midi == 91);
        CHECK(r.notes[1].onsetNum == 1);
        CHECK(r.notes[1].onsetDen == 1);
    }

    TEST_CASE("bar numbers are found as printed, or counted from 1 without them") {
        const BarNumbers printed({"0", "1", "2", "2", "2a", "3"}, 6);
        CHECK(!printed.counted());
        CHECK(printed.find(0) == std::pair{0, 0});
        CHECK(printed.find(2) == std::pair{2, 3});
        CHECK(!printed.find(4));
        CHECK(printed.label(4) == "2a");
        CHECK(printed.describe() == "its bars run from 0 to 3");
        // Any bar without a number, no whole numbers at all, or the wrong count: counted instead.
        for (const BarNumbers& counted : {BarNumbers({"1", "", "3"}, 3), BarNumbers({"A", "B", "C"}, 3),
                                          BarNumbers({"1", "2"}, 3), BarNumbers({}, 3)}) {
            CHECK(counted.counted());
            CHECK(counted.labels() == std::vector<std::string>{"1", "2", "3"});
            CHECK(counted.find(3) == std::pair{2, 2});
        }
    }

    TEST_CASE("finds the four bars of the one-staff scale") {
        const EngraveResult r = engraveScore(testData("scale.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto doc = SvgDocument::fromData(r.svg, error);
        REQUIRE(doc);
        const std::optional<ScoreLayout> layout = findScoreLayout(*doc);
        REQUIRE(layout);
        CHECK(layout->barStarts.size() == 4);
        CHECK(layout->staves.size() == 1);
    }

    TEST_CASE("the pinned column ends after the clefs and key signatures, inside the first bar") {
        const EngraveResult r = engraveScore(testData("piano.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto doc = SvgDocument::fromData(r.svg, error);
        REQUIRE(doc);
        const std::optional<ScoreLayout> layout = findScoreLayout(*doc);
        REQUIRE(layout);
        // A clef and two sharps take more than 4 staff spaces; the time signature comes before bar 2.
        CHECK(layout->pinRight > layout->barStarts[0] + 4 * layout->staffSpace);
        CHECK(layout->pinRight < layout->barStarts[1]);
    }

    TEST_CASE("with a pinned column, the first screen is exactly the document, and scrolled ones join up") {
        const EngraveResult r = engraveScore(testData("piano.musicxml"), defaultVerovioDataDir());
        REQUIRE_MESSAGE(r.error == "", r.error);
        std::string error;
        const auto plain = SvgDocument::fromData(r.svg, error);
        const auto pinned = SvgDocument::fromData(r.svg, error);
        REQUIRE(plain);
        REQUIRE(pinned);
        const std::optional<ScoreLayout> layout = findScoreLayout(*pinned);
        REQUIRE(layout);
        prepareForPaging(*pinned, *layout);

        auto differing = [](const Canvas& a, const Canvas& b, int ax, int bx, int w) {
            int n = 0;
            for (int y = 0; y < a.h; ++y)
                for (int x = 0; x < w; ++x) n += a.at(ax + x, y) != b.at(bx + x, y);
            return n;
        };
        const double scale = 1.5;
        const int w = 400, h = 300, pin = int(std::round(layout->pinRight * scale));
        // Not scrolled: identical, column and all.
        CHECK(differing(plain->render(scale, 0, 20, w, h), pinned->render(scale, 0, 20, w, h), 0, 0, w) == 0);
        // Scrolled 500 pixels: right of the column is the document from 500 + the column's width on.
        const Canvas a = pinned->render(scale, 500, 20, w, h);
        const Canvas b = plain->render(scale, 500 + pin, 20, w, h);
        CHECK(differing(a, b, pin, 0, w - pin) == 0);
        // ...and the column itself is the start of the system, as at the very left.
        const Canvas start = plain->render(scale, 0, 20, w, h);
        CHECK(differing(a, start, 0, 0, pin) == 0);
    }

    TEST_CASE("an SVG that isn't a Verovio score has no layout") {
        std::string error;
        const auto doc = SvgDocument::fromFile(testData("staff-note.svg"), error);
        REQUIRE(doc);
        CHECK(!findScoreLayout(*doc));
    }

    TEST_CASE("a missing file is an error that names it") {
        const EngraveResult r = engraveScore("no/such/score.musicxml", defaultVerovioDataDir());
        CHECK(r.svg.empty());
        CHECK(contains(r.error, "no/such/score.musicxml"));
    }

    TEST_CASE("a file that isn't a score is an error") {
        const EngraveResult r = engraveScore(testData("coverage.svg"), defaultVerovioDataDir());
        CHECK(r.svg.empty());
        CHECK(contains(r.error, "not a score"));
    }

    TEST_CASE("a missing data folder says how to point at another") {
        const EngraveResult r = engraveScore(testData("scale.musicxml"), "no/such/folder");
        CHECK(r.svg.empty());
        CHECK(contains(r.error, "--verovio-data"));
    }
}

TEST_CASE("a bar's label is shown in place of its number where it has one") {
    ScoreLayout layout;
    layout.barMeasures = {0, 1, 2};
    const BarNumbers numbers({"1", "2", "3"}, 3);
    CHECK(barLabels(layout, numbers, {"", "18", ""}) == std::vector<std::string>{"1", "18", "3"});
    CHECK(barLabels(layout, numbers, {}) == std::vector<std::string>{"1", "2", "3"});
}
