#include <doctest/doctest.h>

#include <algorithm>
#include <string>

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

    TEST_CASE("a very tall SVG is rejected instead of allocating a huge image") {
        const SvgRenderResult r = renderSvgData(filledSvg(1, 100, "fill=\"#000\""), 1000);  // 100,000 pixels tall
        CHECK(!r.canvas);
        CHECK(r.error.find("tall") != std::string::npos);
    }
}
