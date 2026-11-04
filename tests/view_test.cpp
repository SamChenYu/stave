#include <doctest/doctest.h>

#include "view.hpp"

// A portrait page, 100 x 200 units, on a 400 x 300 pixel screen: at zoom 1 it's drawn 4 pixels
// per unit, so 400 x 800 pixels, and there's 500 pixels of it below the screen.
TEST_SUITE("view") {
    TEST_CASE("starts at the top-left, fitting the screen's width") {
        const View v(100, 200, 400, 300);
        CHECK(v.zoom() == 1);
        CHECK(v.scale() == 4);
        CHECK(v.x() == 0);
        CHECK(v.y() == 0);
        CHECK(v.maxX() == 0);
        CHECK(v.maxY() == 500);
    }

    TEST_CASE("arrows move a tenth of the screen, pages nine tenths") {
        View v(100, 200, 400, 300);
        CHECK(v.apply(Key::Down));
        CHECK(v.y() == doctest::Approx(30));
        CHECK(v.apply(Key::PageDown));
        CHECK(v.y() == doctest::Approx(300));
        CHECK(v.apply(Key::Up));
        CHECK(v.y() == doctest::Approx(270));
        CHECK(v.apply(Key::PageUp));
        CHECK(v.y() == doctest::Approx(0));
    }

    TEST_CASE("never scrolls past the edges, and says nothing changed when stopped") {
        View v(100, 200, 400, 300);
        CHECK(!v.apply(Key::Up));     // already at the top
        CHECK(!v.apply(Key::Left));   // the image is exactly as wide as the screen
        CHECK(!v.apply(Key::Right));
        for (int i = 0; i < 5; ++i) v.apply(Key::PageDown);
        CHECK(v.y() == 500);
        CHECK(!v.apply(Key::Down));
    }

    TEST_CASE("Home and End jump to the corners") {
        View v(100, 200, 400, 300);
        CHECK(v.apply(Key::End));
        CHECK(v.y() == 500);
        CHECK(v.apply(Key::Home));
        CHECK(v.y() == 0);
    }

    TEST_CASE("zooming keeps the middle of the screen in place") {
        View v(100, 200, 400, 300);
        // The middle of the screen is image point (50, 37.5). At zoom 1.25 (5 pixels per unit) that's
        // pixel (250, 187.5), so the top-left moves to (50, 37.5).
        CHECK(v.apply(Key::ZoomIn));
        CHECK(v.zoom() == doctest::Approx(1.25));
        CHECK(v.x() == doctest::Approx(50));
        CHECK(v.y() == doctest::Approx(37.5));
        // Zoomed in, the image is wider than the screen, so sideways scrolling works now.
        CHECK(v.apply(Key::Right));
        CHECK(v.x() == doctest::Approx(90));
    }

    TEST_CASE("zoom stops at its limits") {
        View v(100, 200, 400, 300);
        for (int i = 0; i < 40; ++i) v.apply(Key::ZoomIn);
        CHECK(v.zoom() == View::kMaxZoom);
        CHECK(!v.apply(Key::ZoomIn));
        for (int i = 0; i < 40; ++i) v.apply(Key::ZoomOut);
        CHECK(v.zoom() == View::kMinZoom);
        CHECK(!v.apply(Key::ZoomOut));
        CHECK(v.x() == 0);  // narrower than the screen: stays against the left edge
        CHECK(v.y() == 0);
    }

    TEST_CASE("resizing keeps the same part of the image at the top-left") {
        View v(100, 200, 400, 300);
        v.apply(Key::PageDown);  // y = 270 pixels = image y 67.5
        v.resize(800, 300);      // twice as wide: 8 pixels per unit
        CHECK(v.scale() == 8);
        CHECK(v.y() == doctest::Approx(540));
    }

    TEST_CASE("resizing taller pulls the view back inside the image") {
        View v(100, 200, 400, 300);
        v.apply(Key::End);    // y = 500
        v.resize(400, 700);   // now only 100 pixels can be below the screen
        CHECK(v.y() == 100);
    }

    TEST_CASE("a wide image, like one long system of music, scrolls sideways once zoomed") {
        View v(1000, 100, 400, 300);  // at zoom 1: 400 x 40 pixels, all on screen
        CHECK(!v.apply(Key::PageDown));
        v.apply(Key::ZoomIn);
        v.apply(Key::ZoomIn);
        v.apply(Key::ZoomIn);  // zoom 1.95: 781 pixels wide
        CHECK(v.maxX() == doctest::Approx(381.25));
        CHECK(v.apply(Key::End));
        CHECK(v.x() == doctest::Approx(381.25));
    }
}

// A score-like image, 1000 x 300 units, on a 400 x 150 screen at 1 pixel per unit (zoom 2.5).
// Five bars start at 50, 250, 450, 650 and 850 and end at 950; moving to a bar keeps a margin of
// 10 to its left, so the stops are at 0 (the first bar shows everything before it), 240, 440, 640
// and 840. The view can go 600 across at most. Two staves own 0-150 and 150-300.
TEST_SUITE("view stops") {
    View scoreView() {
        View v(1000, 300, 400, 150, 2.5);
        View::Stops s;
        s.bars = {50, 250, 450, 650, 850};
        s.barsEnd = 950;
        s.staves = {0, 150};
        s.stavesEnd = 300;
        s.margin = 10;
        v.setStops(s);
        return v;
    }

    TEST_CASE("arrows move a bar at a time, stopping at the edges") {
        View v = scoreView();
        CHECK(v.scale() == 1);
        CHECK(v.apply(Key::Right));
        CHECK(v.x() == 240);
        v.apply(Key::Right);
        CHECK(v.x() == 440);
        v.apply(Key::Right);
        CHECK(v.x() == 600);  // bar 4 would be at 640, past the end of the image
        CHECK(!v.apply(Key::Right));
        v.apply(Key::Left);
        CHECK(v.x() == 440);  // back to the stop before where it is
        v.apply(Key::Left);
        v.apply(Key::Left);
        CHECK(v.x() == 0);
        CHECK(!v.apply(Key::Left));
    }

    TEST_CASE("pages put the bar cut off at the edge first") {
        View v = scoreView();
        CHECK(v.apply(Key::PageRight));
        CHECK(v.x() == 240);  // bar 2 (250-450) was cut off at 400
        v.apply(Key::PageRight);
        CHECK(v.x() == 440);  // bar 3 (450-650) was cut off at 640
        v.apply(Key::PageLeft);
        CHECK(v.x() == 240);  // as far back as possible while x = 440 stays on screen
        v.apply(Key::PageLeft);
        CHECK(v.x() == 0);
    }

    TEST_CASE("up and down move a staff at a time") {
        View v = scoreView();
        CHECK(v.apply(Key::Down));
        CHECK(v.y() == 150);
        CHECK(!v.apply(Key::Down));
        CHECK(v.apply(Key::PageUp));
        CHECK(v.y() == 0);
        CHECK(v.apply(Key::PageDown));
        CHECK(v.y() == 150);
    }

    TEST_CASE("Home and End go to the first and last bar, staying on the same staff") {
        View v = scoreView();
        v.apply(Key::Down);
        CHECK(v.apply(Key::End));
        CHECK(v.x() == 600);
        CHECK(v.y() == 150);
        CHECK(v.apply(Key::Home));
        CHECK(v.x() == 0);
        CHECK(v.y() == 150);
    }

    TEST_CASE("knows which bars and staves are on screen") {
        View v = scoreView();
        CHECK(v.barsOnScreen() == std::pair{1, 2});
        CHECK(v.stavesOnScreen() == std::pair{1, 1});
        v.apply(Key::Right);  // 240-640: bar 1's end shows in the margin, which doesn't count
        CHECK(v.barsOnScreen() == std::pair{2, 3});
        v.apply(Key::End);  // 600-1000
        CHECK(v.barsOnScreen() == std::pair{3, 5});
    }

    TEST_CASE("can go straight to a bar and staff") {
        View v = scoreView();
        v.goTo(3, 2);
        CHECK(v.x() == 440);
        CHECK(v.y() == 150);
        v.goTo(99, 99);  // past the end: as far as it goes
        CHECK(v.x() == 600);
        CHECK(v.y() == 150);
    }

    TEST_CASE("n and N go to the next and previous change, and to its staff if that's off screen") {
        View v = scoreView();
        CHECK(!v.hasChanges());
        CHECK(!v.apply(Key::NextChange));  // no changes: nothing happens
        View::Stops s;
        s.bars = {50, 250, 450, 650, 850};
        s.barsEnd = 950;
        s.staves = {0, 150};
        s.stavesEnd = 300;
        s.margin = 10;
        s.changes = {{1, 2}, {2, 0}, {4, 1}};
        v.setStops(s);
        CHECK(v.hasChanges());
        CHECK(v.apply(Key::NextChange));
        CHECK(v.x() == 240);  // bar 2...
        CHECK(v.y() == 150);  // ...on staff 2, which was off screen
        v.apply(Key::NextChange);
        CHECK(v.x() == 440);
        CHECK(v.y() == 150);  // every staff: stays where it is
        v.apply(Key::NextChange);
        CHECK(v.x() == 600);  // bar 5 is at 840, past the end: as far as it goes
        CHECK(v.y() == 0);    // staff 1
        CHECK(!v.apply(Key::NextChange));  // no more
        v.apply(Key::PreviousChange);
        CHECK(v.x() == 440);
        v.apply(Key::PreviousChange);
        CHECK(v.x() == 240);
        CHECK(!v.apply(Key::PreviousChange));  // none before bar 2, and staff 2 is on screen
    }

    TEST_CASE("without stops, keys move by fractions of the screen as before") {
        View v(1000, 300, 400, 150, 2.5);
        CHECK(!v.hasStops());
        v.apply(Key::Right);
        CHECK(v.x() == doctest::Approx(40));
        CHECK(v.barsOnScreen() == std::pair{0, 0});
    }
}
