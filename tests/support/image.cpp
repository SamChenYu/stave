#include "support/image.hpp"

#include <plutovg.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

// plutovg stores pixels as native-endian 32-bit 0xAARRGGBB with premultiplied alpha, so we
// convert one pixel at a time on the way in and out.

std::optional<Image> loadPng(const std::string& path) {
    plutovg_surface_t* s = plutovg_surface_load_from_image_file(path.c_str());
    if (!s) return std::nullopt;
    Image img(plutovg_surface_get_width(s), plutovg_surface_get_height(s));
    const unsigned char* data = plutovg_surface_get_data(s);
    const int stride = plutovg_surface_get_stride(s);
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            uint32_t argb;
            std::memcpy(&argb, data + size_t(y) * stride + size_t(x) * 4, 4);
            const unsigned a = argb >> 24;
            if (a == 0) continue;  // stays (0, 0, 0, 0)
            auto unpremultiply = [a](unsigned v) { return uint8_t(std::min(255u, (v * 255 + a / 2) / a)); };
            img.at(x, y) = {unpremultiply((argb >> 16) & 0xFF), unpremultiply((argb >> 8) & 0xFF),
                            unpremultiply(argb & 0xFF), uint8_t(a)};
        }
    }
    plutovg_surface_destroy(s);
    return img;
}

bool savePng(const Image& img, const std::string& path) {
    plutovg_surface_t* s = plutovg_surface_create(img.w, img.h);
    if (!s) return false;
    unsigned char* data = plutovg_surface_get_data(s);
    const int stride = plutovg_surface_get_stride(s);
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            const Pixel p = img.at(x, y);
            auto premultiply = [&p](unsigned v) { return (v * p.a + 127) / 255; };
            const uint32_t argb = uint32_t(p.a) << 24 | premultiply(p.r) << 16 | premultiply(p.g) << 8 | premultiply(p.b);
            std::memcpy(data + size_t(y) * stride + size_t(x) * 4, &argb, 4);
        }
    }
    const bool ok = plutovg_surface_write_to_png(s, path.c_str());
    plutovg_surface_destroy(s);
    return ok;
}

ImageDiff compareImages(const Image& actual, const Image& expected, int tolerance) {
    ImageDiff d;
    if (actual.w != expected.w || actual.h != expected.h) {
        d.sameSize = false;
        return d;
    }
    d.visual = Image(expected.w, expected.h);
    for (size_t i = 0; i < expected.px.size(); ++i) {
        const Pixel a = actual.px[i], e = expected.px[i];
        const int delta = std::max({std::abs(a.r - e.r), std::abs(a.g - e.g), std::abs(a.b - e.b), std::abs(a.a - e.a)});
        d.maxDelta = std::max(d.maxDelta, delta);
        if (delta > tolerance) {
            ++d.differing;
            d.visual.px[i] = {255, 0, 0, 255};
        } else {
            // Faded grey version of the expected pixel (over white), so the red stands out.
            const int lum = (e.r * 299 + e.g * 587 + e.b * 114) / 1000;
            const int grey = 255 - (255 - lum) * e.a / 255 / 4;
            d.visual.px[i] = {uint8_t(grey), uint8_t(grey), uint8_t(grey), 255};
        }
    }
    return d;
}
