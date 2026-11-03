#include "support/sixel_decoder.hpp"

#include <map>
#include <vector>

namespace {

class Decoder {
public:
    explicit Decoder(std::string_view s) : s_(s) {}

    SixelDecodeResult run() {
        SixelDecodeResult out;
        out.error = decode();
        if (out.error.empty()) out.image = std::move(img_);
        return out;
    }

private:
    std::string_view s_;
    size_t i_ = 0;
    Image img_;
    bool sized_ = false, drawn_ = false;
    std::map<int, Pixel> palette_;
    int colour_ = -1;
    int x_ = 0, y_ = 0;  // y_ is the top row of the current band

    bool more() const { return i_ < s_.size(); }
    bool isDigit() const { return more() && s_[i_] >= '0' && s_[i_] <= '9'; }

    std::string where() const { return " at byte " + std::to_string(i_); }

    // A decimal number; missing digits count as 0, as in the sixel spec.
    int number() {
        long v = 0;
        while (isDigit()) {
            v = v * 10 + (s_[i_++] - '0');
            if (v > 1'000'000) v = 1'000'000;  // far beyond any real image; keeps the arithmetic safe
        }
        return int(v);
    }

    // Numbers separated by ';', as in "1;1;384;96".
    std::vector<int> numbers() {
        std::vector<int> v{number()};
        while (more() && s_[i_] == ';') {
            ++i_;
            v.push_back(number());
        }
        return v;
    }

    std::string decode() {
        if (s_.substr(0, 2) != "\x1bP") return "doesn't start with ESC P";
        i_ = 2;
        while (more() && (isDigit() || s_[i_] == ';')) ++i_;  // P1;P2;P3: aspect ratio and background mode
        if (!more() || s_[i_] != 'q') return "missing 'q' after the parameters" + where();
        ++i_;

        while (more()) {
            const char c = s_[i_];
            if (c == '\x1b') {
                if (s_.substr(i_) != "\x1b\\") return "expected ESC \\ to end the image, and nothing after it" + where();
                if (!sized_) return "no raster attributes (\"Pan;Pad;Ph;Pv), so the size is unknown";
                return "";
            }
            if (c == '"') {
                ++i_;
                if (sized_ || drawn_) return "raster attributes must come once, before any pixels" + where();
                const std::vector<int> v = numbers();
                if (v.size() != 4) return "raster attributes need 4 numbers" + where();
                img_ = Image(v[2], v[3]);
                sized_ = true;
            } else if (c == '#') {
                ++i_;
                const std::vector<int> v = numbers();
                if (v.size() == 5) {
                    if (v[1] != 2) return "only RGB colours (#n;2;r;g;b) are supported" + where();
                    for (int k = 2; k < 5; ++k)
                        if (v[k] > 100) return "colour component above 100%" + where();
                    auto byte = [](int percent) { return uint8_t((percent * 255 + 50) / 100); };
                    palette_[v[0]] = {byte(v[2]), byte(v[3]), byte(v[4]), 255};
                } else if (v.size() != 1) {
                    return "a colour is either selected (#n) or defined (#n;2;r;g;b)" + where();
                }
                colour_ = v[0];
            } else if (c == '!') {
                ++i_;
                const int count = number();
                if (count < 1 || !more()) return "bad repeat" + where();
                if (std::string err = paint(s_[i_], count); !err.empty()) return err;
                ++i_;
            } else if (c == '$') {
                ++i_;
                x_ = 0;
            } else if (c == '-') {
                ++i_;
                x_ = 0;
                y_ += 6;
            } else if (c >= '?' && c <= '~') {
                if (std::string err = paint(c, 1); !err.empty()) return err;
                ++i_;
            } else {
                return "unexpected byte " + std::to_string(int(uint8_t(c))) + where();
            }
        }
        return "image never ended (missing ESC \\)";
    }

    // Paints `count` columns with the sixel character c: bit r set means row y_ + r gets the colour.
    std::string paint(char c, int count) {
        if (c < '?' || c > '~') return "expected a sixel character" + where();
        if (!sized_) return "pixels before the raster attributes" + where();
        const auto it = palette_.find(colour_);
        if (it == palette_.end()) return "colour " + std::to_string(colour_) + " used before it was defined" + where();
        const int bits = c - '?';
        for (int n = 0; n < count; ++n, ++x_) {
            for (int r = 0; r < 6; ++r) {
                if (!(bits >> r & 1)) continue;
                if (x_ >= img_.w || y_ + r >= img_.h)
                    return "pixel (" + std::to_string(x_) + ", " + std::to_string(y_ + r) + ") is outside the " +
                           std::to_string(img_.w) + "x" + std::to_string(img_.h) + " image" + where();
                img_.at(x_, y_ + r) = it->second;
                drawn_ = true;
            }
        }
        return "";
    }
};

}  // namespace

SixelDecodeResult decodeSixel(std::string_view data) { return Decoder(data).run(); }
