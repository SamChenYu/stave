#pragma once
#include <utility>
#include <vector>

#include "keys.hpp"

// Which part of an image is on screen, and how keys move it.
//
// The image is docW x docH in its own units (an SVG's width and height). Zoom 1 draws it exactly
// as wide as the screen; zoom 2 draws it twice as wide. The offset (x, y) is the screen's top-left
// corner, in pixels of the image as currently drawn, and never goes past the image's edges.
//
// Zoom ranges from kMinZoom times the smaller of the starting zoom and 1 (fitting the width), to
// kMaxZoom times the larger. So an image starting at zoom 1 can go from 0.25 to 8, and a long
// score starting at zoom 12 from 0.25 (all of it across a quarter of the screen) to 96.
class View {
public:
    // Where keys stop in a score, in image units. With stops, arrows move a bar or a staff at a
    // time instead of a tenth of the screen, page keys move by whole bars and staves (the bar or
    // staff cut off at the edge comes first), and Home and End go to the first and last bar. With
    // changes, n and N go to the next and previous one, and to its staff if that's off screen.
    struct Stops {
        std::vector<double> bars;    // where each bar starts, left to right
        double barsEnd = 0;          // where the last bar ends
        std::vector<double> staves;  // where each staff's band starts, top to bottom
        double stavesEnd = 0;        // where the last staff's band ends
        double margin = 0;           // kept to the left of a bar moved to, so its barline shows
        // Where a diff's changes start, left to right, for n and N: a bar (counting from 0), and the
        // staff the change is on (counting from 1), or 0 for every staff.
        std::vector<std::pair<int, int>> changes;
    };

    static constexpr double kMinZoom = 0.25, kMaxZoom = 8, kZoomStep = 1.25;
    static constexpr double kLineFraction = 0.1;  // arrow keys move a tenth of the screen
    static constexpr double kPageFraction = 0.9;  // page keys move a screen, keeping a little in view

    View(double docW, double docH, int screenW, int screenH, double zoom = 1);

    // Applies one key. Returns true if what's on screen changed. Quit and unused keys do nothing.
    bool apply(Key);

    // The window was resized. Keeps the same part of the image at the top-left corner.
    void resize(int screenW, int screenH);

    void setStops(Stops);
    bool hasStops() const { return !stops_.bars.empty(); }
    bool hasChanges() const { return hasStops() && !stops_.changes.empty(); }
    int barCount() const { return int(stops_.bars.size()); }
    int staffCount() const { return int(stops_.staves.size()); }
    // The first and last bar (or staff) at least partly on screen, counting from 1; {0, 0} without stops.
    std::pair<int, int> barsOnScreen() const;
    std::pair<int, int> stavesOnScreen() const;
    // Moves so the given bar and staff (from 1) start at the top-left corner, as far as the edges allow.
    void goTo(int bar, int staff);

    double scale() const { return zoom_ * screenW_ / docW_; }  // pixels per image unit
    double zoom() const { return zoom_; }
    double startZoom() const { return startZoom_; }
    double minZoom() const { return minZoom_; }
    double maxZoom() const { return maxZoom_; }
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
    double startZoom_, minZoom_, maxZoom_;
    double zoom_ = 1, x_ = 0, y_ = 0;
    Stops stops_;
};
