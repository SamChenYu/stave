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
        CHECK(statusLine("song.svg", v, 200).rfind(" song.svg  54%  zoom 100%", 0) == 0);
        v.apply(Key::End);
        CHECK(statusLine("song.svg", v, 200).rfind(" song.svg  end  zoom 100%", 0) == 0);
        const View fits(100, 50, 400, 300);  // 400 x 200 pixels: nothing to scroll
        CHECK(statusLine("song.svg", fits, 200).rfind(" song.svg  all  zoom 100%", 0) == 0);
    }

    TEST_CASE("is padded or cut to exactly the width") {
        const View v(100, 200, 400, 300);
        CHECK(statusLine("song.svg", v, 200).size() == 200);
        CHECK(statusLine("song.svg", v, 12) == " song.svg  t");
        CHECK(statusLine("song.svg", v, 0).empty());
    }
}

TEST_SUITE("view") {
    TEST_CASE("can start at another zoom, within the limits") {
        CHECK(View(100, 200, 400, 300, 2).scale() == 8);
        CHECK(View(100, 200, 400, 300, 100).zoom() == View::kMaxZoom);
    }
}
