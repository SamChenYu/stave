#include "encode.hpp"

#include <algorithm>
#include <charconv>
#include <vector>

namespace {

uint8_t mix(uint8_t paper, uint8_t ink, int coverage) {
    return uint8_t((paper * (255 - coverage) + ink * coverage + 127) / 255);
}

RGB shade(RGB ink, RGB paper, int coverage) {
    return {mix(paper.r, ink.r, coverage), mix(paper.g, ink.g, coverage), mix(paper.b, ink.b, coverage)};
}

void appendInt(std::string& out, int v) {
    char buf[12];
    auto r = std::to_chars(buf, buf + sizeof buf, v);
    out.append(buf, r.ptr);
}

}  // namespace

// Sixel sends the image in bands 6 pixels tall. Each band is painted once per colour: one
// character per column, whose 6 bits say which of that column's 6 pixels get the colour.
std::string encodeSixel(const Canvas& c, RGB ink, RGB paper, bool opaque) {
    constexpr int kLevels = 8;  // shades from paper to ink; enough for smooth edges
    std::string out;
    out.reserve(size_t(c.w) * 8 + 4096);
    out += "\x1bP9;1q";  // start sixel: square pixels, unpainted pixels stay transparent
    out += "\"1;1;";     // raster attributes: 1:1 aspect, then the size in pixels
    appendInt(out, c.w);
    out += ';';
    appendInt(out, c.h);

    // The palette: colour k is k/(kLevels-1) of the way from paper to ink.
    for (int k = 0; k < kLevels; ++k) {
        const RGB col = shade(ink, paper, k * 255 / (kLevels - 1));
        out += '#';
        appendInt(out, k);
        out += ";2";
        for (uint8_t v : {col.r, col.g, col.b}) {
            out += ';';
            appendInt(out, (v * 100 + 127) / 255);  // sixel colours are percentages
        }
    }

    uint8_t levelOf[256];
    for (int v = 0; v < 256; ++v) levelOf[v] = uint8_t((v * (kLevels - 1) + 127) / 255);

    // One pass over each band sorts every pixel into its shade: bits[k][x] holds which of the
    // band's 6 rows have shade k in column x. used[k] is one past the last such column.
    std::vector<uint8_t> bits(size_t(kLevels) * c.w);
    for (int y0 = 0; y0 < c.h; y0 += 6) {
        std::fill(bits.begin(), bits.end(), uint8_t(0));
        int used[kLevels] = {};
        const int rows = std::min(6, c.h - y0);
        for (int r = 0; r < rows; ++r) {
            const uint8_t* src = &c.px[size_t(y0 + r) * c.w];
            const uint8_t bit = uint8_t(1 << r);
            for (int x = 0; x < c.w; ++x) {
                const int k = levelOf[src[x]];
                bits[size_t(k) * c.w + x] |= bit;
                if (x >= used[k]) used[k] = x + 1;
            }
        }

        bool first = true;
        for (int k = opaque ? 0 : 1; k < kLevels; ++k) {  // level 0 is bare paper: skip it to stay transparent
            const int end = used[k];                      // trailing empty columns aren't sent
            if (end == 0) continue;
            if (!first) out += '$';  // back to the start of this band for the next colour
            first = false;
            out += '#';
            appendInt(out, k);

            const uint8_t* row = &bits[size_t(k) * c.w];
            for (int x = 0; x < end;) {
                int run = 1;
                while (x + run < end && row[x + run] == row[x]) ++run;
                const char ch = char(63 + row[x]);
                if (run > 3) {  // run-length encoding: !<count><char>
                    out += '!';
                    appendInt(out, run);
                    out += ch;
                } else {
                    out.append(size_t(run), ch);
                }
                x += run;
            }
        }
        if (y0 + 6 < c.h) out += '-';  // next band
    }
    out += "\x1b\\";  // end sixel
    return out;
}
