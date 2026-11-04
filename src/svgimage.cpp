#include "svgimage.hpp"

#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

struct SvgDocument::Piece {
    lunasvg::Element element;
    lunasvg::Matrix parent;  // the transforms from the document down to the element's parent
    double x0, y0, x1, y1;   // where it is, in document units
};

struct SvgDocument::ToneElement {
    lunasvg::Element element;
    lunasvg::Matrix parent;  // as for a piece
    double x0, y0, x1, y1;   // where it is, in document units
    Tone tone;
};

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

namespace {

// Copies a bitmap's coverage into columns [x0, x0 + bmp width) of c. Pixels are 32-bit ARGB in
// native byte order; alpha (the top byte) is the coverage.
void copyCoverage(const lunasvg::Bitmap& bmp, Canvas& c, int x0) {
    const uint8_t* data = bmp.data();
    for (int row = 0; row < bmp.height() && row < c.h; ++row) {
        const uint8_t* src = data + size_t(row) * bmp.stride();
        for (int col = 0; col < bmp.width() && x0 + col < c.w; ++col) {
            uint32_t argb;
            std::memcpy(&argb, src + size_t(col) * 4, 4);
            c.at(x0 + col, row) = uint8_t(argb >> 24);
        }
    }
}

}  // namespace

void SvgDocument::drawPieces(lunasvg::Bitmap& bmp, double scale, double x, double y,
                             const std::vector<size_t>* only) const {
    const auto s = float(scale);
    const lunasvg::Matrix frame(s, 0, 0, s, -float(x), -float(y));
    if (pieces_.empty()) {
        doc_->render(bmp, frame);
        return;
    }
    // The window in document units, widened a little: a piece's box covers its shapes' geometry,
    // and strokes and soft edges can reach slightly past it.
    const double pad = 4 / scale;
    const double wx0 = x / scale - pad, wx1 = (x + bmp.width()) / scale + pad;
    const double wy0 = y / scale - pad, wy1 = (y + bmp.height()) / scale + pad;
    auto draw = [&](const Piece& p) {
        if (p.x1 >= wx0 && p.x0 <= wx1 && p.y1 >= wy0 && p.y0 <= wy1) p.element.render(bmp, frame * p.parent);
    };
    if (only) {
        for (size_t i : *only)
            if (i < pieces_.size()) draw(pieces_[i]);
    } else {
        for (const Piece& p : pieces_) draw(p);
    }
}

Canvas SvgDocument::render(double scale, double x, double y, int w, int h) const {
    Canvas c(std::max(0, w), std::max(0, h));
    if (w < 1 || h < 1) return c;
    const double rx = std::round(x), ry = std::round(y);
    // With a pinned column: the column on the left, then the document from where it would end.
    const int pin = pinWidth_ > 0 ? std::clamp(int(std::round(pinWidth_ * scale)), 0, w) : 0;
    if (pin > 0) {
        lunasvg::Bitmap column(pin, h);
        if (column.isNull()) return c;  // out of memory: leave it blank rather than fail mid-scroll
        column.clear(0);
        drawPieces(column, scale, 0, ry, &pinPieces_);
        copyCoverage(column, c, 0);
    }
    if (pin < w) {
        lunasvg::Bitmap music(w - pin, h);
        if (music.isNull()) return c;
        music.clear(0);  // fully transparent
        drawPieces(music, scale, rx + pin, ry, nullptr);
        copyCoverage(music, c, pin);
    }
    // Canvas column cx shows the document's pixel column rx + cx, everywhere right of the pin.
    for (const ToneRegion& r : toneRegions_) {
        const int x0 = std::clamp(int(std::lround(r.x0 * scale - rx)), pin, w);
        const int x1 = std::clamp(int(std::lround(r.x1 * scale - rx)), pin, w);
        const int y0 = std::clamp(int(std::lround(r.y0 * scale - ry)), 0, h);
        const int y1 = std::clamp(int(std::lround(r.y1 * scale - ry)), 0, h);
        if (x0 >= x1 || y0 >= y1) continue;
        if (c.tone.empty()) c.tone.assign(c.px.size(), uint8_t(Tone::Plain));
        for (int row = y0; row < y1; ++row)
            std::fill(c.tone.begin() + (ptrdiff_t(row) * w + x0), c.tone.begin() + (ptrdiff_t(row) * w + x1),
                      uint8_t(r.tone));
    }
    markToneElements(c, scale, rx, ry, pin);
    return c;
}

void SvgDocument::markToneElements(Canvas& c, double scale, double rx, double ry, int pin) const {
    const auto s = float(scale);
    for (const ToneElement& e : toneElements_) {
        // Its box on the canvas, widened a little for strokes and soft edges, and kept right of the pin.
        const int x0 = std::clamp(int(std::floor(e.x0 * scale - rx)) - 2, pin, c.w);
        const int x1 = std::clamp(int(std::ceil(e.x1 * scale - rx)) + 2, pin, c.w);
        const int y0 = std::clamp(int(std::floor(e.y0 * scale - ry)) - 2, 0, c.h);
        const int y1 = std::clamp(int(std::ceil(e.y1 * scale - ry)) + 2, 0, c.h);
        if (x0 >= x1 || y0 >= y1) continue;
        lunasvg::Bitmap bmp(x1 - x0, y1 - y0);
        if (bmp.isNull()) continue;
        bmp.clear(0);
        // The bitmap's top-left pixel is canvas pixel (x0, y0), which shows document pixel (rx + x0, ry + y0).
        const lunasvg::Matrix frame(s, 0, 0, s, -float(rx + x0), -float(ry + y0));
        e.element.render(bmp, frame * e.parent);
        const uint8_t* data = bmp.data();
        for (int row = 0; row < bmp.height(); ++row) {
            const uint8_t* src = data + size_t(row) * bmp.stride();
            for (int col = 0; col < bmp.width(); ++col) {
                uint32_t argb;
                std::memcpy(&argb, src + size_t(col) * 4, 4);
                const int own = int(argb >> 24), all = c.at(x0 + col, y0 + row);
                if (own == 0 || 2 * own < all) continue;
                if (c.tone.empty()) c.tone.assign(c.px.size(), uint8_t(Tone::Plain));
                c.tone[size_t(y0 + row) * size_t(c.w) + size_t(x0 + col)] = uint8_t(e.tone);
            }
        }
    }
}

size_t SvgDocument::pieceCount() const { return pieces_.size(); }

std::string SvgDocument::pieceClass(size_t i) const {
    return i < pieces_.size() ? pieces_[i].element.getAttribute("class") : "";
}

void SvgDocument::pinColumn(double width, std::vector<size_t> pieces) {
    pinWidth_ = std::max(0.0, width);
    pinPieces_ = std::move(pieces);
}

namespace {

struct Extent {
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    bool empty() const { return x0 > x1; }
    void add(const Extent& o) {
        x0 = std::min(x0, o.x0);
        y0 = std::min(y0, o.y0);
        x1 = std::max(x1, o.x1);
        y1 = std::max(y1, o.y1);
    }
    void add(const lunasvg::Box& b) {
        x0 = std::min(x0, double(b.x));
        y0 = std::min(y0, double(b.y));
        x1 = std::max(x1, double(b.x + b.w));
        y1 = std::max(y1, double(b.y + b.h));
    }
};

// Grows `ext` to cover what e draws, in document units. lunasvg's own box for a group counts its
// empty children too, as points at their origin; Verovio has many (for dots, articulations and
// the like) whose origin is the page's corner, which would stretch each bar's box back to the start
// of the score. So this goes by the boxes of e's children that cover something, skipping the empty
// ones, and only when none of them does, by e's own box. That last part is for text: lunasvg gives
// a <tspan> no box, but the <text> around it has one. A staff line, which has width but no height,
// still counts.
void addDrawnExtent(const lunasvg::Element& e, Extent& ext) {
    Extent inside;
    for (const lunasvg::Node& node : e.children())
        if (node.isElement()) addDrawnExtent(node.toElement(), inside);
    if (!inside.empty()) {
        ext.add(inside);
        return;
    }
    const lunasvg::Box b = e.getGlobalBoundingBox();
    if (b.w > 0 || b.h > 0) ext.add(b);
}

}  // namespace

size_t SvgDocument::setToneElements(const std::vector<std::pair<std::string, Tone>>& elements) {
    toneElements_.clear();
    for (const auto& [id, tone] : elements) {
        const lunasvg::Element e = doc_->getElementById(id);
        if (!e) continue;
        Extent ext;
        addDrawnExtent(e, ext);
        if (ext.empty()) continue;
        toneElements_.push_back({e, e.parentElement().getGlobalMatrix(), ext.x0, ext.y0, ext.x1, ext.y1, tone});
    }
    return toneElements_.size();
}

size_t SvgDocument::splitInto(const std::string& selector) {
    pieces_.clear();
    for (const lunasvg::Element& container : doc_->querySelectorAll(selector)) {
        for (const lunasvg::Node& node : container.children()) {
            if (!node.isElement()) continue;
            const lunasvg::Element e = node.toElement();
            Extent ext;
            addDrawnExtent(e, ext);
            if (ext.empty()) continue;  // draws nothing (Verovio's milestone markers)
            pieces_.push_back({e, e.parentElement().getGlobalMatrix(), ext.x0, ext.y0, ext.x1, ext.y1});
        }
    }
    return pieces_.size();
}

namespace {

SvgRenderResult failure(std::string message) { return {std::nullopt, std::move(message)}; }

}  // namespace

SvgRenderResult renderSvgDocument(const SvgDocument& doc, int width) {
    if (width < 1 || width > kMaxImageSide)
        return failure("width must be between 1 and " + std::to_string(kMaxImageSide) + " pixels");
    const double scale = width / doc.width();
    const double heightD = std::ceil(doc.height() * scale);
    if (heightD > kMaxImageSide)
        return failure("at " + std::to_string(width) + " pixels wide it would be " + std::to_string(int64_t(heightD)) +
                       " pixels tall, more than the limit of " + std::to_string(kMaxImageSide));
    return {doc.render(scale, 0, 0, width, std::max(1, int(heightD))), ""};
}

SvgRenderResult renderSvgFile(const std::string& path, int width) {
    std::string error;
    const auto doc = SvgDocument::fromFile(path, error);
    if (!doc) return failure(error);
    SvgRenderResult r = renderSvgDocument(*doc, width);
    if (!r.error.empty()) r.error = path + ": " + r.error;
    return r;
}

SvgRenderResult renderSvgData(std::string_view svg, int width) {
    std::string error;
    const auto doc = SvgDocument::fromData(svg, error);
    if (!doc) return failure(error);
    return renderSvgDocument(*doc, width);
}
