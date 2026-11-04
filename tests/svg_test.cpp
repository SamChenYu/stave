#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <utility>

#include "svgimage.hpp"

namespace {

// A plain SVG document of the given size, filled edge to edge with `fill`.
std::string filledSvg(int w, int h, const std::string& fillAttrs) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + std::to_string(w) + " " + std::to_string(h) +
           "\"><rect width=\"" + std::to_string(w) + "\" height=\"" + std::to_string(h) + "\" " + fillAttrs + "/></svg>";
}

bool allPixels(const Canvas& c, uint8_t v) {
    return std::all_of(c.px.begin(), c.px.end(), [v](uint8_t p) { return p == v; });
}

}  // namespace

TEST_SUITE("svg rendering") {
    TEST_CASE("the height follows the aspect ratio") {
        const SvgRenderResult r = renderSvgData(filledSvg(100, 50, "fill=\"#000\""), 200);
        REQUIRE(r.error == "");
        CHECK(r.canvas->w == 200);
        CHECK(r.canvas->h == 100);
        CHECK(allPixels(*r.canvas, 255));
    }

    TEST_CASE("a partial pixel of height rounds up") {
        const SvgRenderResult r = renderSvgData(filledSvg(100, 33, "fill=\"#000\""), 10);  // 3.3 pixels tall
        REQUIRE(r.error == "");
        CHECK(r.canvas->h == 4);
    }

    TEST_CASE("colour is ignored and opacity becomes coverage") {
        const SvgRenderResult red = renderSvgData(filledSvg(10, 10, "fill=\"#d03030\""), 10);
        REQUIRE(red.error == "");
        CHECK(allPixels(*red.canvas, 255));

        const SvgRenderResult half = renderSvgData(filledSvg(10, 10, "fill=\"#000\" fill-opacity=\"0.5\""), 10);
        REQUIRE(half.error == "");
        CHECK(std::all_of(half.canvas->px.begin(), half.canvas->px.end(), [](uint8_t p) { return p >= 127 && p <= 128; }));
    }

    TEST_CASE("nothing drawn means no coverage") {
        const SvgRenderResult r = renderSvgData(filledSvg(10, 10, "fill=\"none\""), 10);
        REQUIRE(r.error == "");
        CHECK(allPixels(*r.canvas, 0));
    }

    TEST_CASE("a missing file is an error that names the file") {
        const SvgRenderResult r = renderSvgFile("no/such/file.svg", 100);
        CHECK(!r.canvas);
        CHECK(r.error.find("no/such/file.svg") != std::string::npos);
    }

    TEST_CASE("text that isn't SVG is an error") {
        const SvgRenderResult r = renderSvgData("this is not an svg file", 100);
        CHECK(!r.canvas);
        CHECK(r.error != "");
    }

    TEST_CASE("an SVG with no size is an error") {
        const SvgRenderResult r = renderSvgData("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"0\" height=\"0\"/>", 100);
        CHECK(!r.canvas);
        CHECK(r.error.find("no size") != std::string::npos);
    }

    TEST_CASE("widths outside the limits are rejected") {
        const std::string svg = filledSvg(10, 10, "fill=\"#000\"");
        for (int w : {0, -1, kMaxImageSide + 1}) {
            CAPTURE(w);
            CHECK(!renderSvgData(svg, w).canvas);
        }
        CHECK(renderSvgData(svg, 1).canvas);
    }

    TEST_CASE("a window drawn at an offset is exactly that part of the whole image") {
        std::string error;
        const auto doc = SvgDocument::fromFile(std::string(STAVE_TEST_DATA_DIR) + "/staff-note.svg", error);
        REQUIRE_MESSAGE(doc, error);
        const double scale = 3;  // 600 x 240 pixels in all
        const Canvas whole = doc->render(scale, 0, 0, 600, 240);
        for (auto [x, y] : {std::pair{0, 0}, {137, 41}, {450, 100}}) {
            CAPTURE(x);
            CAPTURE(y);
            const Canvas part = doc->render(scale, x, y, 150, 140);
            int wrong = 0;
            for (int py = 0; py < part.h; ++py)
                for (int px = 0; px < part.w; ++px) {
                    const int wx = x + px, wy = y + py;
                    const uint8_t want = wx < whole.w && wy < whole.h ? whole.at(wx, wy) : 0;  // beyond: blank
                    if (part.at(px, py) != want) ++wrong;
                }
            CHECK(wrong == 0);
        }
    }

    TEST_CASE("the window's offset is rounded to whole pixels") {
        std::string error;
        const auto doc = SvgDocument::fromData(filledSvg(10, 10, "fill=\"#000\""), error);
        REQUIRE(doc);
        const Canvas c = doc->render(1, -0.4, 4.6, 10, 10);  // same as (0, 5): rows 5 and up are past the image
        CHECK(c.at(0, 4) == 255);
        CHECK(c.at(0, 5) == 0);
    }

    TEST_CASE("an empty window is fine") {
        std::string error;
        const auto doc = SvgDocument::fromData(filledSvg(10, 10, "fill=\"#000\""), error);
        REQUIRE(doc);
        CHECK(doc->render(1, 0, 0, 0, 5).px.empty());
    }

    TEST_CASE("drawing only nearby pieces keeps text, though lunasvg gives its tspans no box") {
        // Like Verovio's part names: the size is on an inner <tspan>, and the <text> says 0px.
        const std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"60\"><g class=\"system\">"
            "<g class=\"label\"><text x=\"10\" y=\"40\" font-size=\"0px\"><tspan><tspan font-size=\"30px\">"
            "Violin</tspan></tspan></text></g>"
            "<g class=\"measure\"><rect x=\"300\" y=\"10\" width=\"80\" height=\"40\"/></g></g></svg>";
        std::string error;
        const auto whole = SvgDocument::fromData(svg, error);
        const auto split = SvgDocument::fromData(svg, error);
        REQUIRE(whole);
        REQUIRE(split);
        CHECK(split->splitInto(".system") == 2);
        const Canvas a = whole->render(1, 0, 0, 400, 60), b = split->render(1, 0, 0, 400, 60);
        int textPixels = 0;  // in the left half, where only the text is
        for (int y = 0; y < a.h; ++y)
            for (int x = 0; x < 200; ++x) textPixels += a.at(x, y) > 0;
        CHECK(textPixels > 100);
        CHECK(a.px == b.px);
    }

    TEST_CASE("tone regions mark the pixels they cover, wherever the window is") {
        std::string error;
        auto doc = SvgDocument::fromData(filledSvg(100, 50, "fill=\"#000\""), error);
        REQUIRE(doc);
        CHECK(doc->render(2, 0, 0, 200, 100).tone.empty());  // no regions: no tones at all
        doc->setToneRegions({{10, 5, 30, 20, Tone::Added}, {25, 0, 40, 10, Tone::Removed}});
        // At 2 pixels per unit, offset by (6, 4): the first region covers canvas pixels x 14-53 and
        // y 6-35, the second x 44-73 and y 0-15.
        const Canvas c = doc->render(2, 6, 4, 100, 50);
        REQUIRE(c.tone.size() == c.px.size());
        CHECK(c.toneAt(13, 6) == Tone::Plain);  // just left of the first region
        CHECK(c.toneAt(14, 6) == Tone::Added);  // its top-left corner
        CHECK(c.toneAt(53, 35) == Tone::Added);  // its bottom-right corner
        CHECK(c.toneAt(54, 35) == Tone::Plain);
        CHECK(c.toneAt(53, 36) == Tone::Plain);
        CHECK(c.toneAt(50, 15) == Tone::Removed);  // where they overlap, the second wins
        CHECK(c.toneAt(50, 16) == Tone::Added);
        CHECK(c.toneAt(73, 0) == Tone::Removed);
        CHECK(c.toneAt(74, 0) == Tone::Plain);
    }

    TEST_CASE("regions outside the window leave the canvas without tones") {
        std::string error;
        auto doc = SvgDocument::fromData(filledSvg(100, 50, "fill=\"#000\""), error);
        REQUIRE(doc);
        doc->setToneRegions({{80, 0, 100, 50, Tone::Changed}});
        CHECK(doc->render(1, 0, 0, 50, 50).tone.empty());
    }

    TEST_CASE("the pinned column stays plain") {
        std::string error;
        auto doc = SvgDocument::fromData(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"10\"><g class=\"system\">"
            "<rect x=\"0\" width=\"100\" height=\"10\"/></g></svg>",
            error);
        REQUIRE(doc);
        REQUIRE(doc->splitInto(".system") == 1);
        doc->pinColumn(20, {0});
        doc->setToneRegions({{0, 0, 100, 10, Tone::Added}});
        const Canvas c = doc->render(1, 30, 0, 50, 10);
        CHECK(c.toneAt(19, 5) == Tone::Plain);
        CHECK(c.toneAt(20, 5) == Tone::Added);
    }

    TEST_CASE("a very tall SVG is rejected instead of allocating a huge image") {
        const SvgRenderResult r = renderSvgData(filledSvg(1, 100, "fill=\"#000\""), 1000);  // 100,000 pixels tall
        CHECK(!r.canvas);
        CHECK(r.error.find("tall") != std::string::npos);
    }
}
