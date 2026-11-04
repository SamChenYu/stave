#include <doctest/doctest.h>

#include <ostream>  // lets doctest print std::string
#include <string>
#include <vector>

#include "marks.hpp"

namespace {

std::vector<Mark> parsed(const std::string& value, Tone tone = Tone::Added) {
    std::vector<Mark> marks;
    std::string error;
    REQUIRE_MESSAGE(parseMarks(value, tone, marks, error), error);
    return marks;
}

std::string parseError(const std::string& value, Tone tone = Tone::Added) {
    std::vector<Mark> marks;
    std::string error;
    CHECK(!parseMarks(value, tone, marks, error));
    CHECK(marks.empty());
    return error;
}

// checkMarks on a score of `bars` bars numbered from 1, and `staves` staves.
std::string check(std::vector<Mark> marks, int bars, int staves) {
    return checkMarks(marks, BarNumbers({}, bars), staves);
}

// Marks found in a score with these bar numbers (as checkMarks finds them), and two staves.
std::vector<Mark> found(const std::string& value, Tone tone = Tone::Added,
                        const std::vector<std::string>& numbers = {"1", "2", "3"}) {
    std::vector<Mark> marks = parsed(value, tone);
    REQUIRE(checkMarks(marks, BarNumbers(numbers, int(numbers.size())), 2) == "");
    return marks;
}

bool same(const Mark& m, Tone tone, int first, int last, int staff) {
    return m.tone == tone && m.firstBar == first && m.lastBar == last && m.staff == staff;
}

// Three bars starting at x = 100, 200 and 300 on a page 500 wide and 400 tall, with two staves.
ScoreLayout threeBarsTwoStaves() {
    ScoreLayout l;
    l.barStarts = {100, 200, 300};
    l.end = 450;
    l.staves = {{50, 122, 0, 180}, {238, 310, 180, 400}};
    return l;
}

}  // namespace

TEST_SUITE("marks") {
    TEST_CASE("a bar, a range, and either on one staff") {
        const std::vector<Mark> m = parsed("7,12-15,20@3,30-32@2", Tone::Changed);
        REQUIRE(m.size() == 4);
        CHECK(same(m[0], Tone::Changed, 7, 7, 0));
        CHECK(same(m[1], Tone::Changed, 12, 15, 0));
        CHECK(same(m[2], Tone::Changed, 20, 20, 3));
        CHECK(same(m[3], Tone::Changed, 30, 32, 2));
    }

    TEST_CASE("spaces around the parts are fine, and a one-bar range is a bar") {
        const std::vector<Mark> m = parsed(" 4 - 6 @ 1 , 9-9 ");
        REQUIRE(m.size() == 2);
        CHECK(same(m[0], Tone::Added, 4, 6, 1));
        CHECK(same(m[1], Tone::Added, 9, 9, 0));
    }

    TEST_CASE("each option adds to the marks so far") {
        std::vector<Mark> marks;
        std::string error;
        REQUIRE(parseMarks("1", Tone::Added, marks, error));
        REQUIRE(parseMarks("2", Tone::Removed, marks, error));
        REQUIRE(marks.size() == 2);
        CHECK(marks[1].tone == Tone::Removed);
    }

    TEST_CASE("mistakes are reported with the option's name, and add nothing") {
        CHECK(parseError("").find("--added needs bars") == 0);
        CHECK(parseError("3,,4").find("--added needs bars") == 0);
        CHECK(parseError("3,").find("--added needs bars") == 0);
        CHECK(parseError("y", Tone::Removed).find("--removed: 'y' isn't a bar") == 0);
        CHECK(parseError("x").find("isn't a bar") != std::string::npos);
        CHECK(parseError("-3").find("isn't a bar") != std::string::npos);
        CHECK(parseError("3-").find("isn't a bar") != std::string::npos);
        CHECK(parseError("1-2-3").find("isn't a bar") != std::string::npos);
        CHECK(parseError("2.5").find("isn't a bar") != std::string::npos);
        CHECK(parseError("5@").find("on a staff") != std::string::npos);
        CHECK(parseError("5@0").find("on a staff") != std::string::npos);
        CHECK(parseError("9-4", Tone::Changed) == "--changed: '9-4' isn't a range: it ends before it starts");
        CHECK(parseError("1,2,x").find("'x'") != std::string::npos);  // nothing from 1 and 2 is kept
    }

    TEST_CASE("a pickup is often bar 0") {
        const std::vector<Mark> m = parsed("0,0-2");
        REQUIRE(m.size() == 2);
        CHECK(same(m[0], Tone::Added, 0, 0, 0));
        CHECK(same(m[1], Tone::Added, 0, 2, 0));
    }

    TEST_CASE("marks are written back the way they're given") {
        CHECK(describeMark({Tone::Added, 7, 7, 0}) == "7");
        CHECK(describeMark({Tone::Added, 12, 15, 0}) == "12-15");
        CHECK(describeMark({Tone::Added, 12, 15, 2}) == "12-15@2");
    }

    TEST_CASE("marks must be within the score") {
        CHECK(check(parsed("1-3@2"), 3, 2) == "");
        CHECK(check(parsed("2-4"), 3, 2) == "--added 2-4: the score has no bar 4; its bars run from 1 to 3");
        CHECK(check(parsed("0"), 3, 2) == "--added 0: the score has no bar 0; its bars run from 1 to 3");
        CHECK(check(parsed("1@3", Tone::Removed), 3, 2) == "--removed 1@3: the score has only 2 staves");
        CHECK(check(parsed("2"), 1, 1) == "--added 2: the score has no bar 2; its only bar is 1");
        CHECK(check(parsed("1@2"), 1, 1) == "--added 1@2: the score has only 1 staff");
    }

    TEST_CASE("marks go by the printed bar numbers") {
        // A pickup bar 0, then bars 1 to 3.
        const std::vector<std::string> pickup{"0", "1", "2", "3"};
        std::vector<Mark> m = found("0,1-2,3", Tone::Added, pickup);
        CHECK(m[0].firstIndex == 0);
        CHECK(m[0].lastIndex == 0);
        CHECK(m[1].firstIndex == 1);
        CHECK(m[1].lastIndex == 2);
        CHECK(m[2].firstIndex == 3);
        m = parsed("4");
        CHECK(checkMarks(m, BarNumbers(pickup, 4), 2) == "--added 4: the score has no bar 4; its bars run from 0 to 3");
    }

    TEST_CASE("a range takes in bars numbered otherwise, and a number on two bars marks both") {
        const std::vector<std::string> odd{"11", "12", "12a", "13", "14", "14"};
        std::vector<Mark> m = found("12-13,12,14", Tone::Changed, odd);
        CHECK(m[0].firstIndex == 1);
        CHECK(m[0].lastIndex == 3);  // 12a too
        CHECK(m[1].firstIndex == 1);
        CHECK(m[1].lastIndex == 1);  // just 12
        CHECK(m[2].firstIndex == 4);
        CHECK(m[2].lastIndex == 5);  // both 14s
    }

    TEST_CASE("a range must run forwards through the score") {
        std::vector<Mark> m = parsed("3-5");
        CHECK(checkMarks(m, BarNumbers({"5", "4", "3"}, 3), 1) == "--added 3-5: bar 5 comes before bar 3 in this score");
    }

    TEST_CASE("a bar can't be marked in two different tones, on the same staff") {
        auto both = [](const std::string& a, Tone ta, const std::string& b, Tone tb) {
            std::vector<Mark> marks = parsed(a, ta);
            for (const Mark& m : parsed(b, tb)) marks.push_back(m);
            return check(marks, 10, 3);
        };
        CHECK(both("3-5", Tone::Changed, "4@2", Tone::Added) ==
              "--added 4@2 overlaps --changed 3-5: a bar can't be both changed and added");
        CHECK(both("4@2", Tone::Added, "1-9@2", Tone::Removed) ==
              "--removed 1-9@2 overlaps --added 4@2: a bar can't be both added and removed");
        CHECK(both("4@1", Tone::Added, "4", Tone::Changed) ==
              "--changed 4 overlaps --added 4@1: a bar can't be both added and changed");
        // Next to each other, or the same bar on another staff, is fine...
        CHECK(both("3-5", Tone::Changed, "6-7", Tone::Added) == "");
        CHECK(both("4@1", Tone::Changed, "4@2,4@3", Tone::Added) == "");
        // ...and so is the same tone twice.
        CHECK(both("3-5", Tone::Changed, "4@2,1-10", Tone::Changed) == "");
    }

    TEST_CASE("a mark outside the score is reported before any overlap") {
        std::vector<Mark> marks = parsed("1-3", Tone::Changed);
        for (const Mark& m : parsed("2,9")) marks.push_back(m);
        CHECK(check(marks, 5, 1) == "--added 9: the score has no bar 9; its bars run from 1 to 5");
    }

    TEST_CASE("each bar is a region between its barlines") {
        ScoreLayout l = threeBarsTwoStaves();
        l.barInsides = {{102, 197}, {203, 297}, {303, 440}};
        const auto r = toneRegions(found("1,2-3"), l, 500, 400);
        REQUIRE(r.size() == 3);
        CHECK(r[0].x0 == 102);
        CHECK(r[0].x1 == 197);
        CHECK(r[1].x0 == 203);
        CHECK(r[1].x1 == 297);
        CHECK(r[2].x0 == 303);
        CHECK(r[2].x1 == 440);
    }

    TEST_CASE("without barlines found, a bar runs from its start to the next bar's, the last to the page's edge") {
        const auto r = toneRegions(found("1,2-3"), threeBarsTwoStaves(), 500, 400);
        REQUIRE(r.size() == 3);
        CHECK(r[0].x0 == 100);
        CHECK(r[0].x1 == 200);
        CHECK(r[1].x0 == 200);
        CHECK(r[1].x1 == 300);
        CHECK(r[2].x0 == 300);
        CHECK(r[2].x1 == 500);
    }

    TEST_CASE("a mark on one staff covers its band; on every staff, the page's height") {
        const auto r = toneRegions(found("2@1,2@2", Tone::Removed), threeBarsTwoStaves(), 500, 400);
        const auto all = toneRegions(found("2", Tone::Removed), threeBarsTwoStaves(), 500, 400);
        REQUIRE(r.size() == 2);
        CHECK(r[0].y0 == 0);
        CHECK(r[0].y1 == 180);
        CHECK(r[1].y0 == 180);
        CHECK(r[1].y1 == 400);
        REQUIRE(all.size() == 1);
        CHECK(all[0].y0 == 0);
        CHECK(all[0].y1 == 400);
        CHECK(all[0].tone == Tone::Removed);
    }

    TEST_CASE("marks that checkMarks hasn't found are left out") {
        CHECK(toneRegions(parsed("1,2@1"), threeBarsTwoStaves(), 500, 400).empty());
    }
}
