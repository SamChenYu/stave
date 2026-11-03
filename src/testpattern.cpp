#include "testpattern.hpp"

#include <algorithm>
#include <cmath>

Canvas testPattern() {
    Canvas c(384, 96);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < c.w; ++x) c.at(x, y) = uint8_t(x * 255 / (c.w - 1));
    for (int y = 48; y < c.h; ++y) {
        for (int x = 0; x < 192; ++x) c.at(x, y) = (x / 8 + (y - 48) / 8) % 2 ? 255 : 0;
        for (int x = 192; x < c.w; ++x) {
            const float d = std::hypot(x + 0.5f - 288, y + 0.5f - 72) - 20;  // distance from the disc's edge
            c.at(x, y) = uint8_t(std::clamp(0.5f - d, 0.0f, 1.0f) * 255 + 0.5f);
        }
    }
    return c;
}
