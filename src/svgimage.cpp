#include "svgimage.hpp"

#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace {

SvgRenderResult failure(std::string message) { return {std::nullopt, std::move(message)}; }

}  // namespace

SvgRenderResult renderSvgFile(const std::string& path, int width) {
    // Read the file ourselves (lunasvg's loadFromFile can't tell "missing" from "not SVG").
    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("can't open " + path);
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    SvgRenderResult r = renderSvgData(data, width);
    if (!r.error.empty()) r.error = path + ": " + r.error;
    return r;
}

SvgRenderResult renderSvgData(std::string_view svg, int width) {
    if (width < 1 || width > kMaxImageSide)
        return failure("width must be between 1 and " + std::to_string(kMaxImageSide) + " pixels");

    const std::unique_ptr<lunasvg::Document> doc = lunasvg::Document::loadFromData(svg.data(), svg.size());
    if (!doc) return failure("not a valid SVG file");
    const float docW = doc->width(), docH = doc->height();
    if (!(docW > 0 && docH > 0)) return failure("the SVG has no size (it needs width and height, or a viewBox)");

    const double heightD = std::ceil(double(width) * docH / docW);
    if (heightD > kMaxImageSide)
        return failure("at " + std::to_string(width) + " pixels wide it would be " + std::to_string(int64_t(heightD)) +
                       " pixels tall, more than the limit of " + std::to_string(kMaxImageSide));
    const int height = std::max(1, int(heightD));

    lunasvg::Bitmap bmp(width, height);
    if (bmp.isNull()) return failure("out of memory for a " + std::to_string(width) + "x" + std::to_string(height) + " image");
    bmp.clear(0);  // fully transparent
    doc->render(bmp, lunasvg::Matrix(width / docW, 0, 0, height / docH, 0, 0));

    // Pixels are 32-bit ARGB in native byte order; alpha (the top byte) is the coverage.
    Canvas c(width, height);
    const uint8_t* data = bmp.data();
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = data + size_t(y) * bmp.stride();
        for (int x = 0; x < width; ++x) {
            uint32_t argb;
            std::memcpy(&argb, row + size_t(x) * 4, 4);
            c.at(x, y) = uint8_t(argb >> 24);
        }
    }
    return {std::move(c), ""};
}
