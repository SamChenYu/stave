#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "canvas.hpp"

namespace lunasvg {
class Document;
}

// The largest image we'll render, in each direction: far beyond any terminal, small enough
// that a mistyped width can't ask for gigabytes.
constexpr int kMaxImageSide = 16384;

// A parsed SVG, kept so that any part of it can be drawn again and again (as the pager scrolls
// and zooms) without parsing it each time.
//
// Drawing produces ink coverage: how opaque each pixel is. Colours are ignored, so a red shape
// and a black one come out the same; the ink colour is chosen when the canvas is encoded.
class SvgDocument {
public:
    // nullptr on failure, with the reason in `error`.
    static std::unique_ptr<SvgDocument> fromFile(const std::string& path, std::string& error);
    static std::unique_ptr<SvgDocument> fromData(std::string_view svg, std::string& error);
    ~SvgDocument();

    // Size in the SVG's own units; both are always more than 0.
    double width() const { return width_; }
    double height() const { return height_; }

    // Draws a w x h pixel window of the image as drawn at `scale` pixels per unit. (x, y) is the
    // window's top-left corner, in those pixels; it's rounded to whole pixels so that scrolling
    // moves the picture exactly, without its edges shimmering. Anywhere outside the image is blank.
    Canvas render(double scale, double x, double y, int w, int h) const;

    // Speeds up drawing small windows of a big document. The children of every element matching
    // the CSS selector become pieces, and render() then draws only the pieces near the window,
    // instead of the whole document. Anything outside those elements is no longer drawn, so the
    // selector must cover all that's visible: for Verovio's scores, ".system" (its children are the
    // bars, the brace and the system's opening barline). Returns how many pieces there are; with
    // none, render() goes on drawing everything.
    size_t splitInto(const std::string& selector);

    // The parsed document itself, for reading its structure (as score.cpp does).
    const lunasvg::Document& lunasvgDocument() const { return *doc_; }

private:
    struct Piece;
    explicit SvgDocument(std::unique_ptr<lunasvg::Document>);

    std::unique_ptr<lunasvg::Document> doc_;
    double width_ = 0, height_ = 0;
    std::vector<Piece> pieces_;
};

struct SvgRenderResult {
    std::optional<Canvas> canvas;  // set on success
    std::string error;             // set on failure
};

// The whole SVG, `width` pixels wide, with the height following its aspect ratio.
SvgRenderResult renderSvgDocument(const SvgDocument&, int width);
SvgRenderResult renderSvgFile(const std::string& path, int width);
SvgRenderResult renderSvgData(std::string_view svg, int width);
