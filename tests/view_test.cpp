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
