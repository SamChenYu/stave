#pragma once
#include "keys.hpp"

// Which part of an image is on screen, and how keys move it.
//
// The image is docW x docH in its own units (an SVG's width and height). Zoom 1 draws it exactly
// as wide as the screen; zoom 2 draws it twice as wide. The offset (x, y) is the screen's top-left
// corner, in pixels of the image as currently drawn, and never goes past the image's edges.
class View {
public:
    static constexpr double kMinZoom = 0.25, kMaxZoom = 8, kZoomStep = 1.25;
    static constexpr double kLineFraction = 0.1;  // arrow keys move a tenth of the screen
    static constexpr double kPageFraction = 0.9;  // page keys move a screen, keeping a little in view

    View(double docW, double docH, int screenW, int screenH);

    // Applies one key. Returns true if what's on screen changed. Quit and unused keys do nothing.
    bool apply(Key);

    // The window was resized. Keeps the same part of the image at the top-left corner.
    void resize(int screenW, int screenH);

    double scale() const { return zoom_ * screenW_ / docW_; }  // pixels per image unit
    double zoom() const { return zoom_; }
    double x() const { return x_; }
    double y() const { return y_; }
    int screenW() const { return screenW_; }
    int screenH() const { return screenH_; }
    double imageW() const { return docW_ * scale(); }  // the whole image, as currently drawn
    double imageH() const { return docH_ * scale(); }
    double maxX() const;
    double maxY() const;

private:
    void setZoom(double zoom);  // keeps the point at the middle of the screen in place
    void clamp();

    double docW_, docH_;
    int screenW_, screenH_;
    double zoom_ = 1, x_ = 0, y_ = 0;
};
