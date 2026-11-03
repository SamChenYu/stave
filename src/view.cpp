#include "view.hpp"

#include <algorithm>

View::View(double docW, double docH, int screenW, int screenH, double zoom)
    : docW_(std::max(docW, 1e-6)), docH_(std::max(docH, 0.0)), screenW_(std::max(1, screenW)),
      screenH_(std::max(1, screenH)) {
    zoom = std::max(zoom, 1e-6);
    startZoom_ = zoom;
    minZoom_ = kMinZoom * std::min(1.0, zoom);
    maxZoom_ = kMaxZoom * std::max(1.0, zoom);
    zoom_ = zoom;
}

double View::maxX() const { return std::max(0.0, imageW() - screenW_); }
double View::maxY() const { return std::max(0.0, imageH() - screenH_); }

void View::clamp() {
    x_ = std::clamp(x_, 0.0, maxX());
    y_ = std::clamp(y_, 0.0, maxY());
}

void View::setZoom(double zoom) {
    zoom = std::clamp(zoom, minZoom_, maxZoom_);
    const double s = scale();
    const double cx = (x_ + screenW_ / 2.0) / s, cy = (y_ + screenH_ / 2.0) / s;  // image point at the middle
    zoom_ = zoom;
    x_ = cx * scale() - screenW_ / 2.0;
    y_ = cy * scale() - screenH_ / 2.0;
    clamp();
}

bool View::apply(Key k) {
    const double oldX = x_, oldY = y_, oldZoom = zoom_;
    const double lineX = screenW_ * kLineFraction, lineY = screenH_ * kLineFraction;
    const double pageX = screenW_ * kPageFraction, pageY = screenH_ * kPageFraction;
    switch (k) {
        case Key::Left: x_ -= lineX; break;
        case Key::Right: x_ += lineX; break;
        case Key::Up: y_ -= lineY; break;
        case Key::Down: y_ += lineY; break;
        case Key::PageLeft: x_ -= pageX; break;
        case Key::PageRight: x_ += pageX; break;
        case Key::PageUp: y_ -= pageY; break;
        case Key::PageDown: y_ += pageY; break;
        case Key::Home: x_ = y_ = 0; break;
        case Key::End:
            x_ = maxX();
            y_ = maxY();
            break;
        case Key::ZoomIn: setZoom(zoom_ * kZoomStep); break;
        case Key::ZoomOut: setZoom(zoom_ / kZoomStep); break;
        default: break;
    }
    clamp();
    return x_ != oldX || y_ != oldY || zoom_ != oldZoom;
}

void View::resize(int screenW, int screenH) {
    const double s = scale();
    const double ix = x_ / s, iy = y_ / s;  // image point at the top-left corner
    screenW_ = std::max(1, screenW);
    screenH_ = std::max(1, screenH);
    x_ = ix * scale();
    y_ = iy * scale();
    clamp();
}
