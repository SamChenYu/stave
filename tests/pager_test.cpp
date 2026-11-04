#include <doctest/doctest.h>

#include <string>

#include "pager.hpp"

TEST_SUITE("pager layout") {
    TEST_CASE("the image covers every row but the status line, in whole sixel bands") {
        const PagerLayout l = pagerLayout(120, 30, 9, 19);
        CHECK(l.imageRows == 29);
        CHECK(l.imageW == 1080);
        CHECK(l.imageH == 546);  // 29 rows x 19 = 551 pixels, rounded down to a multiple of 6
    }

    TEST_CASE("an unknown cell size is guessed, leaving a spare row in case the guess is too big") {
        const PagerLayout l = pagerLayout(80, 24, 0, 0);
        CHECK(l.imageRows == 23);
        CHECK(l.imageW == 800);  // 80 columns x 10
        CHECK(l.imageH == 438);  // 22 rows x 20 = 440, rounded down to a multiple of 6
    }

    TEST_CASE("a one-row window still gets an image") {
        const PagerLayout l = pagerLayout(10, 1, 9, 19);
        CHECK(l.imageRows == 1);
        CHECK(l.imageH == 18);
    }

    TEST_CASE("for braille, the image is in dots: 2 across and 4 down per character") {
        const PagerLayout l = pagerLayout(120, 30, 9, 19, Output::Braille);
        CHECK(l.imageRows == 29);
        CHECK(l.imageW == 240);
        CHECK(l.imageH == 116);
    }

    TEST_CASE("absurd sizes are capped") {
        const PagerLayout l = pagerLayout(100000, 100000, 50, 50);
        CHECK(l.imageW == kMaxImageSide);
        CHECK(l.imageH <= kMaxImageSide);
    }
}

// The same portrait page as in view_test.cpp: 100 x 200 units on a 400 x 300 screen, with
// 500 pixels below the screen at zoom 1.
TEST_SUITE("status line") {
    TEST_CASE("says where the view is") {
        View v(100, 200, 400, 300);
        CHECK(statusLine("song.svg", v, 200).rfind(" song.svg  top  zoom 100%", 0) == 0);
        v.apply(Key::PageDown);  // 270 of 500
        CHECK(statusLine("song.svg", v, 200).rfind(" song.svg  54% down  zoom 100%", 0) == 0);
        v.apply(Key::End);
        CHECK(statusLine("song.svg", v, 200).rfind(" song.svg  bottom  zoom 100%", 0) == 0);
        const View fits(100, 50, 400, 300);  // 400 x 200 pixels: nothing to scroll
        CHECK(statusLine("song.svg", fits, 200).rfind(" song.svg  all  zoom 100%", 0) == 0);
    }

    TEST_CASE("says where the view is across a wide image, like a score") {
        View v(1000, 100, 400, 300, 2);  // 800 x 80 pixels: 400 to the right of the screen
        CHECK(statusLine("score.mxl", v, 200).rfind(" score.mxl  left  zoom 100%", 0) == 0);
        v.apply(Key::PageRight);  // 360 of 400
        CHECK(statusLine("score.mxl", v, 200).rfind(" score.mxl  90% across  zoom 100%", 0) == 0);
        v.apply(Key::End);
        CHECK(statusLine("score.mxl", v, 200).rfind(" score.mxl  right  zoom 100%", 0) == 0);
    }

    TEST_CASE("says both directions when the image is bigger both ways") {
        const View v(100, 100, 400, 300, 2);  // 800 x 800 pixels
        CHECK(statusLine("big.svg", v, 200).rfind(" big.svg  left, top  zoom 100%", 0) == 0);
    }

    TEST_CASE("gives the zoom relative to where it started") {
        View v(1000, 100, 400, 300, 12);  // a long score, opened at 12 times fitting the width
        CHECK(statusLine("score.mxl", v, 200).find("zoom 100%") != std::string::npos);
        v.apply(Key::ZoomIn);
        CHECK(statusLine("score.mxl", v, 200).find("zoom 125%") != std::string::npos);
    }

    TEST_CASE("for a score, says which bars and staves are on screen") {
        View v(1000, 300, 400, 150, 2.5);  // as in view_test.cpp's "view stops"
        View::Stops s;
        s.bars = {50, 250, 450, 650, 850};
        s.barsEnd = 950;
        s.staves = {0, 150};
        s.stavesEnd = 300;
        s.margin = 10;
        v.setStops(s);
        CHECK(statusLine("quartet.mxl", v, 200).rfind(" quartet.mxl  bars 1-2 of 5  staff 1 of 2  zoom 100%", 0) == 0);
        CHECK(statusLine("quartet.mxl", v, 200).find("arrows/hjkl bar/staff") != std::string::npos);
        // With the score's printed bar numbers: a pickup bar 0, then 1 to 4.
        CHECK(statusLine("quartet.mxl", v, 200, {"0", "1", "2", "3", "4"}).rfind(" quartet.mxl  bars 0-1 of 4  ", 0) == 0);
    }

    TEST_CASE("is padded or cut to exactly the width") {
        const View v(100, 200, 400, 300);
        CHECK(statusLine("song.svg", v, 200).size() == 200);
        CHECK(statusLine("song.svg", v, 12) == " song.svg  t");
        CHECK(statusLine("song.svg", v, 0).empty());
    }
}

TEST_SUITE("view") {
    TEST_CASE("can start at another zoom, and the limits follow it") {
        const View v(100, 200, 400, 300, 2);
        CHECK(v.scale() == 8);
        CHECK(v.minZoom() == View::kMinZoom);       // out to a quarter of fitting the width
        CHECK(v.maxZoom() == 2 * View::kMaxZoom);   // in to 8 times the start
        const View tiny(100, 200, 400, 300, 0.5);
        CHECK(tiny.minZoom() == 0.5 * View::kMinZoom);
        CHECK(tiny.maxZoom() == View::kMaxZoom);
    }
}

// A long score: 10,000 units wide and 300 tall, 18 units per staff space, on a screen of
// 1080 x 551 pixels (29 rows of 19 pixels) once the status line is taken off.
TEST_SUITE("score zoom") {
    const PagerLayout kLayout = pagerLayout(120, 30, 9, 19);  // image 1080 x 546

    TEST_CASE("staves start about two and a half rows tall, not squeezed to fit the width") {
        // 2.5 rows x 19 pixels / 4 spaces = 11.875 pixels per space = 0.66 pixels per unit; the
        // system is then 198 pixels tall, more than twice short of 546, so it's doubled to 1.32.
        const double zoom = scoreStartZoom(10000, 300, 18, kLayout, 2.5 * 19 / 4);
        const double scale = zoom * kLayout.imageW / 10000;
        CHECK(scale == doctest::Approx(2 * 2.5 * 19 / 4 / 18));
    }

    TEST_CASE("a system that nearly fits is enlarged just enough to fill the height") {
        const double zoom = scoreStartZoom(10000, 600, 18, kLayout, 2.5 * 19 / 4);  // 396 pixels tall at the start size
        const double scale = zoom * kLayout.imageW / 10000;
        CHECK(scale == doctest::Approx(546.0 / 600));
    }

    TEST_CASE("a tall system, like a full orchestra, keeps the readable size and scrolls down") {
        const double zoom = scoreStartZoom(10000, 3000, 18, kLayout, 2.5 * 19 / 4);
        const double scale = zoom * kLayout.imageW / 10000;
        CHECK(scale == doctest::Approx(2.5 * 19 / 4 / 18));
    }
}
