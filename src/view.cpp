#include "view.hpp"

#include <algorithm>
#include <limits>

namespace {

// One direction's stops, measured in pixels at the current scale.
struct Axis {
    const std::vector<double>& starts;  // image units
    double end;                         // image units
    double margin;                      // image units
    double scale;

    int count() const { return int(starts.size()); }
    // Where the view goes to show item i first. The first item shows everything before it too.
    double target(int i) const { return i == 0 ? 0 : (starts[size_t(i)] - margin) * scale; }
    double itemEnd(int i) const { return (i + 1 < count() ? starts[size_t(i) + 1] : end) * scale; }

    double next(double pos) const {
        for (int i = 0; i < count(); ++i)
            if (target(i) > pos + 0.5) return target(i);
        return std::numeric_limits<double>::max();  // past the last: clamped to the far edge
    }
    double previous(double pos) const {
        for (int i = count() - 1; i >= 0; --i)
            if (target(i) < pos - 0.5) return target(i);
        return 0;
    }
    // A screen on: the last item that starts on screen comes first (or the next, if that's this one).
    double pageForward(double pos, double screen) const {
        int last = -1;
        for (int i = 0; i < count(); ++i)
            if (starts[size_t(i)] * scale < pos + screen) last = i;
        if (last < 0 || target(last) <= pos + 0.5) return next(pos);
        return target(last);
    }
    // A screen back: as far as possible while what was first stays on screen.
    double pageBack(double pos, double screen) const {
        for (int i = 0; i < count(); ++i)
            if (target(i) >= pos - screen) return target(i) < pos - 0.5 ? target(i) : previous(pos);
        return previous(pos);
    }
    std::pair<int, int> onScreen(double pos, double screen) const {
        int first = 0, last = 0;
        for (int i = 0; i < count(); ++i) {
            // The margin left of a bar shows the end of the one before; that doesn't count.
            if (starts[size_t(i)] * scale < pos + screen && itemEnd(i) > pos + margin * scale + 0.5) {
                if (first == 0) first = i + 1;
                last = i + 1;
            }
        }
        return {first, last};
    }
};

}  // namespace

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
    if (hasStops()) {
        const Axis bars{stops_.bars, stops_.barsEnd, stops_.margin, scale()};
        const Axis staves{stops_.staves, stops_.stavesEnd, 0, scale()};
        switch (k) {
            case Key::Left: x_ = bars.previous(x_); break;
            case Key::Right: x_ = bars.next(x_); break;
            case Key::PageLeft: x_ = bars.pageBack(x_, screenW_); break;
            case Key::PageRight: x_ = bars.pageForward(x_, screenW_); break;
            case Key::Home: x_ = 0; break;
            case Key::End: x_ = maxX(); break;
            case Key::Up: y_ = staves.previous(y_); break;
            case Key::Down: y_ = staves.next(y_); break;
            case Key::PageUp: y_ = staves.pageBack(y_, screenH_); break;
            case Key::PageDown: y_ = staves.pageForward(y_, screenH_); break;
            case Key::ZoomIn: setZoom(zoom_ * kZoomStep); break;
            case Key::ZoomOut: setZoom(zoom_ / kZoomStep); break;
            case Key::NextChange:
            case Key::PreviousChange: {
                const bool forward = k == Key::NextChange;
                const std::pair<int, int>* to = nullptr;
                for (const std::pair<int, int>& c : stops_.changes) {
                    if (c.first < 0 || c.first >= bars.count()) continue;
                    const double t = std::min(bars.target(c.first), maxX());  // as far as it can go
                    if (forward ? t > x_ + 0.5 : t < x_ - 0.5) {
                        to = &c;
                        if (forward) break;  // the first one past here; going back, the last one before
                    }
                }
                if (!to) break;
                x_ = bars.target(to->first);
                const std::pair<int, int> shown = staves.onScreen(y_, screenH_);
                if (to->second > 0 && to->second <= staves.count() &&
                    (to->second < shown.first || to->second > shown.second))
                    y_ = staves.target(to->second - 1);
                break;
            }
            default: break;
        }
        clamp();
        return x_ != oldX || y_ != oldY || zoom_ != oldZoom;
    }
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

void View::setStops(Stops stops) { stops_ = std::move(stops); }

std::pair<int, int> View::barsOnScreen() const {
    return Axis{stops_.bars, stops_.barsEnd, stops_.margin, scale()}.onScreen(x_, screenW_);
}

std::pair<int, int> View::stavesOnScreen() const {
    return Axis{stops_.staves, stops_.stavesEnd, 0, scale()}.onScreen(y_, screenH_);
}

void View::goTo(int bar, int staff) {
    if (!hasStops()) return;
    const Axis bars{stops_.bars, stops_.barsEnd, stops_.margin, scale()};
    const Axis staves{stops_.staves, stops_.stavesEnd, 0, scale()};
    x_ = bars.target(std::clamp(bar, 1, bars.count()) - 1);
    if (staves.count() > 0) y_ = staves.target(std::clamp(staff, 1, staves.count()) - 1);
    clamp();
}
