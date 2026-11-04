#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "color.hpp"

namespace lunasvg {
class Bitmap;
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

    size_t pieceCount() const;
    std::string pieceClass(size_t i) const;  // the class attribute of piece i

    // Keeps the first `width` units of the document at the left edge while the rest scrolls
    // sideways, drawn from just the given pieces (from splitInto). When render() is asked for a
    // window that doesn't start at the left edge, its left part shows that column, and the rest
    // shows the document from where the column would end: at x = 0, the two meet seamlessly and
    // the picture is exactly the document. Scrolling still runs to the same end.
    void pinColumn(double width, std::vector<size_t> pieces);

    // A rectangle, in document units, whose ink render() draws in a tone, to show a diff.
    struct ToneRegion {
        double x0, y0, x1, y1;
        Tone tone;
    };
    // Sets where ink is drawn in a tone other than plain; later regions win where they overlap.
    // The pinned column (see pinColumn) stays plain: it repeats the opening clefs and key
    // signatures, whichever bars are on screen.
    void setToneRegions(std::vector<ToneRegion> regions) { toneRegions_ = std::move(regions); }

    // Elements, by id, whose ink render() draws in a tone: single notes, rather than whole bars. A
    // pixel takes an element's tone where the element is at least half its ink, so a notehead takes
    // the bit of staff line under it, but not the rest of the line. These win over the regions, and
    // the pinned column stays plain. Ids not in the document are skipped; returns how many were found.
    size_t setToneElements(const std::vector<std::pair<std::string, Tone>>& elements);

    // The parsed document itself, for reading its structure (as score.cpp does).
    const lunasvg::Document& lunasvgDocument() const { return *doc_; }

private:
    struct Piece;
    struct ToneElement;
    explicit SvgDocument(std::unique_ptr<lunasvg::Document>);
    void markToneElements(Canvas& c, double scale, double rx, double ry, int pin) const;

    std::unique_ptr<lunasvg::Document> doc_;
    double width_ = 0, height_ = 0;
    std::vector<Piece> pieces_;
    double pinWidth_ = 0;              // document units; 0: no pinned column
    std::vector<size_t> pinPieces_;    // indices into pieces_
    std::vector<ToneRegion> toneRegions_;
    std::vector<ToneElement> toneElements_;

    // Draws the pieces near the window (all of them, or those in `only`) into bmp, whose top-left
    // corner is at (x, y) in pixels of the document drawn at `scale`.
    void drawPieces(lunasvg::Bitmap& bmp, double scale, double x, double y,
                    const std::vector<size_t>* only) const;
};

struct SvgRenderResult {
    std::optional<Canvas> canvas;  // set on success
    std::string error;             // set on failure
};

// The whole SVG, `width` pixels wide, with the height following its aspect ratio.
SvgRenderResult renderSvgDocument(const SvgDocument&, int width);
SvgRenderResult renderSvgFile(const std::string& path, int width);
SvgRenderResult renderSvgData(std::string_view svg, int width);
