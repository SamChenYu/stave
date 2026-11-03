#include <doctest/doctest.h>

#include <initializer_list>
#include <string>
#include <utility>

#include "encode.hpp"
#include "support/golden.hpp"
#include "support/sixel_decoder.hpp"
#include "svgimage.hpp"
#include "termprobe.hpp"
#include "testpattern.hpp"
#include "view.hpp"

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

// One pager frame: what's on a w x h screen showing an SVG from tests/data, after pressing `keys`.
Canvas frameAfter(const std::string& name, int w, int h, std::initializer_list<Key> keys) {
    std::string error;
    const auto doc = SvgDocument::fromFile(std::string(STAVE_TEST_DATA_DIR) + "/" + name, error);
    REQUIRE_MESSAGE(doc, error);
    View v(doc->width(), doc->height(), w, h);
    for (Key k : keys) v.apply(k);
    return doc->render(v.scale(), v.x(), v.y(), w, h);
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

    TEST_CASE("SVG in a dark terminal's own colours") {
        // What `stave --svg` draws when the terminal reports light grey text on near-black.
        TerminalInfo dark;
        dark.fg = RGB{204, 204, 204};
        dark.bg = RGB{12, 12, 12};
        const InkAndPaper c = chooseColours(dark);
        checkGolden("svg-staff-note-dark", throughSixel(svgFromTestData("staff-note.svg", 400), c.ink, c.paper, c.opaque));
    }
}

// Pager frames of staff-note.svg (200 x 80 units) on a 300 x 150 pixel screen.
TEST_SUITE("golden pager frames") {
    TEST_CASE("fitted to the screen's width, with blank space below") {
        checkGolden("frame-fit", throughSixel(frameAfter("staff-note.svg", 300, 150, {}), kBlack, kWhite, true));
    }

    TEST_CASE("zoomed in twice, then one step right and down") {
        const Canvas c = frameAfter("staff-note.svg", 300, 150, {Key::ZoomIn, Key::ZoomIn, Key::Right, Key::Down});
        checkGolden("frame-zoomed-scrolled", throughSixel(c, kBlack, kWhite, true));
    }

    TEST_CASE("zoomed in, then End: the bottom-right corner") {
        const Canvas c =
            frameAfter("staff-note.svg", 300, 150, {Key::ZoomIn, Key::ZoomIn, Key::ZoomIn, Key::ZoomIn, Key::End});
        checkGolden("frame-end", throughSixel(c, kBlack, kWhite, true));
    }

    TEST_CASE("zoomed out: smaller than the screen, against the top-left") {
        const Canvas c = frameAfter("staff-note.svg", 300, 150, {Key::ZoomOut, Key::ZoomOut});
        checkGolden("frame-zoomed-out", throughSixel(c, kBlack, kWhite, true));
    }
}
