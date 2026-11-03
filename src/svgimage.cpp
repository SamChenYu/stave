#include "svgimage.hpp"

#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

SvgDocument::SvgDocument(std::unique_ptr<lunasvg::Document> doc)
    : doc_(std::move(doc)), width_(doc_->width()), height_(doc_->height()) {}

SvgDocument::~SvgDocument() = default;

std::unique_ptr<SvgDocument> SvgDocument::fromFile(const std::string& path, std::string& error) {
    // Read the file ourselves (lunasvg's loadFromFile can't tell "missing" from "not SVG").
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "can't open " + path;
        return nullptr;
    }
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto doc = fromData(data, error);
    if (!doc) error = path + ": " + error;
    return doc;
}

std::unique_ptr<SvgDocument> SvgDocument::fromData(std::string_view svg, std::string& error) {
    std::unique_ptr<lunasvg::Document> doc = lunasvg::Document::loadFromData(svg.data(), svg.size());
    if (!doc) {
        error = "not a valid SVG file";
        return nullptr;
    }
    if (!(doc->width() > 0 && doc->height() > 0)) {
        error = "the SVG has no size (it needs width and height, or a viewBox)";
        return nullptr;
    }
    return std::unique_ptr<SvgDocument>(new SvgDocument(std::move(doc)));
}

Canvas SvgDocument::render(double scale, double x, double y, int w, int h) const {
    Canvas c(std::max(0, w), std::max(0, h));
    if (w < 1 || h < 1) return c;
    lunasvg::Bitmap bmp(w, h);
    if (bmp.isNull()) return c;  // out of memory: leave it blank rather than fail mid-scroll
    bmp.clear(0);                // fully transparent
    const auto s = float(scale);
    doc_->render(bmp, lunasvg::Matrix(s, 0, 0, s, -float(std::round(x)), -float(std::round(y))));

    // Pixels are 32-bit ARGB in native byte order; alpha (the top byte) is the coverage.
    const uint8_t* data = bmp.data();
    for (int row = 0; row < h; ++row) {
        const uint8_t* src = data + size_t(row) * bmp.stride();
        for (int col = 0; col < w; ++col) {
            uint32_t argb;
            std::memcpy(&argb, src + size_t(col) * 4, 4);
            c.at(col, row) = uint8_t(argb >> 24);
        }
    }
    return c;
}

namespace {

SvgRenderResult failure(std::string message) { return {std::nullopt, std::move(message)}; }

SvgRenderResult renderWhole(const SvgDocument& doc, int width) {
    if (width < 1 || width > kMaxImageSide)
        return failure("width must be between 1 and " + std::to_string(kMaxImageSide) + " pixels");
    const double scale = width / doc.width();
    const double heightD = std::ceil(doc.height() * scale);
    if (heightD > kMaxImageSide)
        return failure("at " + std::to_string(width) + " pixels wide it would be " + std::to_string(int64_t(heightD)) +
                       " pixels tall, more than the limit of " + std::to_string(kMaxImageSide));
    return {doc.render(scale, 0, 0, width, std::max(1, int(heightD))), ""};
}

}  // namespace

SvgRenderResult renderSvgFile(const std::string& path, int width) {
    std::string error;
    const auto doc = SvgDocument::fromFile(path, error);
    if (!doc) return failure(error);
    SvgRenderResult r = renderWhole(*doc, width);
    if (!r.error.empty()) r.error = path + ": " + r.error;
    return r;
}

SvgRenderResult renderSvgData(std::string_view svg, int width) {
    std::string error;
    const auto doc = SvgDocument::fromData(svg, error);
    if (!doc) return failure(error);
    return renderWhole(*doc, width);
}
