#include "encode.hpp"

#include <lunasvg.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <vector>

namespace {

uint8_t mix(uint8_t paper, uint8_t ink, int coverage) {
    return uint8_t((paper * (255 - coverage) + ink * coverage + 127) / 255);
}

RGB shade(RGB ink, RGB paper, int coverage) {
    return {mix(paper.r, ink.r, coverage), mix(paper.g, ink.g, coverage), mix(paper.b, ink.b, coverage)};
}

std::string base64(const std::string& in) {
    static const char* const kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = uint32_t(uint8_t(in[i])) << 16 | uint32_t(uint8_t(in[i + 1])) << 8 | uint8_t(in[i + 2]);
        out += kTable[v >> 18];
        out += kTable[(v >> 12) & 63];
        out += kTable[(v >> 6) & 63];
        out += kTable[v & 63];
    }
    if (i < in.size()) {  // one or two bytes left: pad with '='
        const bool two = i + 1 < in.size();
        const uint32_t v = uint32_t(uint8_t(in[i])) << 16 | (two ? uint32_t(uint8_t(in[i + 1])) << 8 : 0);
        out += kTable[v >> 18];
        out += kTable[(v >> 12) & 63];
        out += two ? kTable[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
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

std::string encodeKitty(const Canvas& c, RGB ink, RGB paper, bool opaque, bool moveCursor) {
    const int bytesPerPixel = opaque ? 3 : 4;
    std::string pixels(c.px.size() * size_t(bytesPerPixel), '\0');
    for (size_t i = 0; i < c.px.size(); ++i) {
        const RGB p = opaque ? shade(ink, paper, c.px[i]) : ink;
        char* d = &pixels[i * size_t(bytesPerPixel)];
        d[0] = char(p.r);
        d[1] = char(p.g);
        d[2] = char(p.b);
        if (!opaque) d[3] = char(c.px[i]);
    }
    const std::string data = base64(pixels);
    constexpr size_t kChunk = 4096;
    std::string out;
    for (size_t i = 0; i == 0 || i < data.size(); i += kChunk) {
        out += "\x1b_G";
        if (i == 0) {
            // a=T: send and show it; f: 24-bit RGB or 32-bit RGBA; q=2: send no replies;
            // C=1: leave the cursor where it is.
            out += opaque ? "a=T,f=24,q=2," : "a=T,f=32,q=2,";
            if (!moveCursor) out += "C=1,";
            out += "s=";
            appendInt(out, c.w);
            out += ",v=";
            appendInt(out, c.h);
            out += ',';
        }
        out += i + kChunk < data.size() ? "m=1;" : "m=0;";  // m=1: more chunks follow
        out.append(data, i, kChunk);
        out += "\x1b\\";
    }
    return out;
}

std::vector<std::string> encodeBraille(const Canvas& c) {
    // Which bit of a braille character raises the dot at (column dx, row dy) of its 2x4 cell.
    static const uint8_t kBit[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
    std::vector<std::string> rows;
    for (int y0 = 0; y0 < c.h; y0 += 4) {
        std::string row;
        row.reserve(size_t(c.w + 1) / 2 * 3);
        for (int x0 = 0; x0 < c.w; x0 += 2) {
            int dots = 0;
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = x0 + dx, y = y0 + dy;
                    if (x < c.w && y < c.h && c.at(x, y) >= kBrailleThreshold) dots |= kBit[dy][dx];
                }
            const int cp = 0x2800 + dots;  // braille patterns start at U+2800; UTF-8 needs 3 bytes
            row += char(0xE0 | (cp >> 12));
            row += char(0x80 | ((cp >> 6) & 0x3F));
            row += char(0x80 | (cp & 0x3F));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool writePng(const Canvas& c, RGB ink, RGB paper, const std::string& path) {
    lunasvg::Bitmap bmp(c.w, c.h);  // 32-bit ARGB in native byte order; opaque, so not premultiplied
    if (bmp.isNull()) return false;
    uint8_t* data = bmp.data();
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            const RGB p = shade(ink, paper, c.at(x, y));
            const uint32_t argb = 0xFF000000u | uint32_t(p.r) << 16 | uint32_t(p.g) << 8 | p.b;
            std::memcpy(data + size_t(y) * bmp.stride() + size_t(x) * 4, &argb, 4);
        }
    }
    return bmp.writeToPng(path);
}
