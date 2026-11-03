#include <doctest/doctest.h>

#include <string>
#include <utility>

#include "encode.hpp"
#include "support/golden.hpp"
#include "support/sixel_decoder.hpp"
#include "svgimage.hpp"
#include "testpattern.hpp"

// Pixel comparisons against the expected images in tests/golden/: each case draws a known input,
// runs it through the encoder, decodes the bytes the way a terminal would, and compares pixels.
// To accept a deliberate change, run with STAVE_UPDATE_GOLDEN=1 and review the new PNGs.

namespace {

// What a terminal shows for this canvas: encode it to sixel, then decode those bytes.
Image throughSixel(const Canvas& c, RGB ink, RGB paper, bool opaque) {
    SixelDecodeResult r = decodeSixel(encodeSixel(c, ink, paper, opaque));
    REQUIRE_MESSAGE(r.error.empty(), r.error);
    return std::move(r.image);
}

// Renders an SVG from tests/data, as `stave --svg` does.
Canvas svgFromTestData(const std::string& name, int width) {
    SvgRenderResult r = renderSvgFile(std::string(STAVE_TEST_DATA_DIR) + "/" + name, width);
    REQUIRE_MESSAGE(r.canvas, r.error);
    return std::move(*r.canvas);
}

constexpr RGB kBlack{0, 0, 0}, kWhite{255, 255, 255};

}  // namespace

TEST_SUITE("golden images") {
    TEST_CASE("test pattern, black ink on white paper") {
        checkGolden("test-pattern-opaque", throughSixel(testPattern(), {0, 0, 0}, {255, 255, 255}, true));
    }

    TEST_CASE("test pattern, light ink on a transparent background") {
        // Paper only tints the soft edges here; bare paper is left unpainted.
        checkGolden("test-pattern-transparent", throughSixel(testPattern(), {230, 230, 230}, {30, 30, 30}, false));
    }

    TEST_CASE("SVG: staff lines, notes and a slur") {
        checkGolden("svg-staff-note", throughSixel(svgFromTestData("staff-note.svg", 400), kBlack, kWhite, true));
    }

    TEST_CASE("SVG: colour is ignored, opacity becomes coverage") {
        checkGolden("svg-coverage", throughSixel(svgFromTestData("coverage.svg", 360), kBlack, kWhite, true));
    }
}
