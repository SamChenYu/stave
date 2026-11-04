#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "compare.hpp"
#include "pager.hpp"

namespace {

// Bars numbered 1 to n.
BarNumbers counted(int n) { return BarNumbers({}, n); }

std::vector<BarRun> pairs(const std::string& value) {
    std::vector<BarRun> runs;
    std::string error;
    REQUIRE(parsePairs(value, runs, error));
    return runs;
}

// A score's stops: `bars` bars, each 100 units wide, and `staves` staves, each 50 tall.
View::Stops stops(int bars, int staves) {
    View::Stops s;
    for (int i = 0; i < bars; ++i) s.bars.push_back(100.0 * i);
    s.barsEnd = 100.0 * bars;
    for (int i = 0; i < staves; ++i) s.staves.push_back(50.0 * i);
    s.stavesEnd = 50.0 * staves;
    return s;
}

}  // namespace

TEST_SUITE("compare: pairs") {
    TEST_CASE("runs of bars, and single bars") {
        const std::vector<BarRun> runs = pairs("1-17=1-17, 19-40=18-39,41=45");
        REQUIRE(runs.size() == 3);
        CHECK(runs[1].firstOld == 19);
        CHECK(runs[1].lastOld == 40);
        CHECK(runs[1].firstNew == 18);
        CHECK(runs[1].lastNew == 39);
        CHECK(runs[2].firstOld == 41);
        CHECK(runs[2].lastNew == 45);
    }

    TEST_CASE("mistakes are explained, and nothing is appended") {
        std::vector<BarRun> runs;
        std::string error;
        CHECK(!parsePairs("1-17", runs, error));
        CHECK(error.find("'1-17' isn't a run of old bars = new bars") != std::string::npos);
        CHECK(!parsePairs("1-17=1-16", runs, error));
        CHECK(error == "--pairs: '1-17=1-16' matches different numbers of bars");
        CHECK(!parsePairs("1-5=1-5,,", runs, error));
        CHECK(runs.empty());
    }
}

TEST_SUITE("compare: bar map") {
    TEST_CASE("without runs, bars are matched in order") {
        BarMap map;
        CHECK(mapBars({}, counted(3), counted(4), map) == "");
        CHECK(map.newToOld == std::vector<int>{0, 1, 2, 2});
        CHECK(map.oldToNew == std::vector<int>{0, 1, 2});
    }

    TEST_CASE("an inserted bar sits beside the old bar where it was put in") {
        // New bar 3 was inserted: old bars 1-2 are new 1-2, old 3-4 are new 4-5.
        BarMap map;
        CHECK(mapBars(pairs("1-2=1-2,3-4=4-5"), counted(4), counted(5), map) == "");
        CHECK(map.newToOld == std::vector<int>{0, 1, 2, 2, 3});
        CHECK(map.oldToNew == std::vector<int>{0, 1, 3, 4});
    }

    TEST_CASE("a deleted bar goes to the new bar after where it was") {
        // Old bar 2 was deleted.
        BarMap map;
        CHECK(mapBars(pairs("1=1,3-4=2-3"), counted(4), counted(3), map) == "");
        CHECK(map.oldToNew == std::vector<int>{0, 1, 1, 2});
        CHECK(map.newToOld == std::vector<int>{0, 2, 3});
    }

    TEST_CASE("bars go by their printed numbers") {
        BarMap map;
        const BarNumbers pickup({"0", "1", "2"}, 3);
        CHECK(mapBars(pairs("0-2=0-2"), pickup, pickup, map) == "");
        CHECK(map.newToOld == std::vector<int>{0, 1, 2});
        CHECK(mapBars(pairs("1-3=1-3"), pickup, counted(3), map) == "--pairs: the old version has no bar 3");
    }
}

TEST_SUITE("compare: screen") {
    TEST_CASE("the screen splits into two halves and a one-character gap") {
        const SplitWidths s = splitWidths(1600, 10);  // 160 characters
        CHECK(s.left == 790);
        CHECK(s.gap == 10);
        CHECK(s.right == 800);
        const SplitWidths braille = splitWidths(161, 2);  // 80 characters; the odd dot is dropped
        CHECK(braille.left == 78);
        CHECK(braille.gap == 2);
        CHECK(braille.right == 80);
    }

    TEST_CASE("the halves are put side by side, keeping their tones, with a line between") {
        Canvas left(2, 1), right(3, 2);
        left.at(0, 0) = 10;
        right.at(2, 1) = 20;
        right.tone.assign(right.px.size(), uint8_t(Tone::Plain));
        right.tone[5] = uint8_t(Tone::Added);  // (2, 1)
        const Canvas c = sideBySide(left, right, 4);
        CHECK(c.w == 9);
        CHECK(c.h == 2);
        CHECK(c.at(0, 0) == 10);
        CHECK(c.at(8, 1) == 20);
        CHECK(c.toneAt(8, 1) == Tone::Added);
        CHECK(c.toneAt(0, 0) == Tone::Plain);
        CHECK(c.at(2, 0) == 0);    // the gap, pixels 2 to 5...
        CHECK(c.at(3, 0) == 255);  // ...with a line down its middle
        CHECK(c.at(3, 1) == 255);
        CHECK(c.at(5, 1) == 0);
    }

    TEST_CASE("the status line names each version under its half") {
        CompareOptions opt;
        opt.before.name = "HEAD";
        opt.after.name = "working tree";
        View before(1000, 200, 300, 200, 3), after(1000, 200, 300, 200, 3);
        before.setStops(stops(10, 4));
        after.setStops(stops(10, 4));
        const CompareViews views{before, after};
        const std::string s = compareStatusLine(opt, views, 30, 120);
        CHECK(s.size() == 120);
        CHECK(s.rfind(" HEAD  bars 1-4 of 10", 0) == 0);
        CHECK(s.substr(30, 28) == "| working tree  bars 1-4 of ");
    }
}

TEST_SUITE("compare: following") {
    TEST_CASE("the old version follows the new to the matching bar and staff") {
        // New bar 3 was inserted, so new bar 5 is old bar 4.
        BarMap map;
        REQUIRE(mapBars(pairs("1-2=1-2,3-4=4-5"), counted(4), counted(5), map) == "");
        View before(400, 200, 100, 50, 4), after(500, 200, 100, 50, 5);  // 1 pixel per unit
        before.setStops(stops(4, 4));
        after.setStops(stops(5, 4));
        CompareViews views{before, after};

        after.goTo(5, 2);
        views.after = after;
        followView(views.before, views.after, map);
        CHECK(views.before.barsOnScreen().first == 4);
        CHECK(views.before.stavesOnScreen().first == 2);

        // Beside the inserted bar, the old version shows where it was put in.
        views.after.goTo(3, 1);
        followView(views.before, views.after, map);
        CHECK(views.before.barsOnScreen().first == 3);
    }

    TEST_CASE("keys move the new version, the old one follows, and zooming zooms both") {
        BarMap map;
        REQUIRE(mapBars({}, counted(5), counted(5), map) == "");
        View before(500, 200, 100, 50, 5), after(500, 200, 100, 50, 5);
        before.setStops(stops(5, 4));
        after.setStops(stops(5, 4));
        CompareViews views{before, after};

        CHECK(applyCompareKey(views, Key::Right, map));
        CHECK(views.after.barsOnScreen().first == 2);
        CHECK(views.before.barsOnScreen().first == 2);

        const double scale = views.before.scale();
        CHECK(applyCompareKey(views, Key::ZoomOut, map));
        CHECK(views.before.scale() < scale);
        CHECK(views.before.scale() == doctest::Approx(views.after.scale()));
    }
}
