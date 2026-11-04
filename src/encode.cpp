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
std::string encodeSixel(const Canvas& c, const Inks& inks, RGB paper, bool opaque) {
    constexpr int kLevels = 8;  // shades from paper to ink; enough for smooth edges
    constexpr int kMaxColours = kToneCount * (1 + kToneCount * (kLevels - 1));  // every fill, tone and shade
    std::string out;
    out.reserve(size_t(c.w) * 8 + 4096);
    out += "\x1bP9;1q";  // start sixel: square pixels, unpainted pixels stay transparent
    out += "\"1;1;";     // raster attributes: 1:1 aspect, then the size in pixels
    appendInt(out, c.w);
    out += ';';
    appendInt(out, c.h);

    // The palette: colour 0 is bare paper. Then each tone in the picture gets kLevels - 1 colours,
    // shade k being k/(kLevels-1) of the way from paper to that tone's ink. Without marks, that's
    // colours 0 to 7, from paper to ink. Filled pixels get the same again over their tinted paper,
    // with shade 0 being the tinted paper itself. colourOf[key(f, t, k)] is the colour for shade k
    // of tone t over fill f.
    uint8_t levelOf[256];
    for (int v = 0; v < 256; ++v) levelOf[v] = uint8_t((v * (kLevels - 1) + 127) / 255);
    const auto key = [](int f, int t, int k) { return (f * kToneCount + (k == 0 ? 0 : t)) * kLevels + k; };
    std::vector<int> colourOf(size_t(kToneCount * kToneCount * kLevels), -1);
    // Bare paper is always colour 0, and plain ink always has all its shades, as before.
    for (int k = 0; k < kLevels; ++k) colourOf[size_t(key(0, 0, k))] = 0;
    // Each tone in the picture gets all its shades, over each background it's on.
    bool onBack[kToneCount][kToneCount] = {};  // [fill][tone]: in the picture
    onBack[0][0] = true;
    for (size_t i = 0; i < c.px.size(); ++i) {
        const int f = c.fill.empty() || c.fill[i] >= kToneCount ? 0 : c.fill[i];
        const int t = c.tone.empty() || c.tone[i] >= kToneCount ? 0 : c.tone[i];
        onBack[f][t] = true;
    }
    for (int f = 0; f < kToneCount; ++f)
        for (int t = 0; t < kToneCount; ++t)
            for (int k = 0; onBack[f][t] && k < kLevels; ++k) colourOf[size_t(key(f, t, k))] = 0;  // numbered below

    auto define = [&](int colour, RGB col) {
        out += '#';
        appendInt(out, colour);
        out += ";2";
        for (uint8_t v : {col.r, col.g, col.b}) {
            out += ';';
            appendInt(out, (v * 100 + 127) / 255);  // sixel colours are percentages
        }
    };
    int colours = 0;
    for (int f = 0; f < kToneCount; ++f) {
        const RGB back = f == 0 ? paper : fillPaper(inks, paper, Tone(f));
        for (int t = 0; t < kToneCount; ++t)
            for (int k = 0; k < kLevels; ++k) {
                int& colour = colourOf[size_t(key(f, t, k))];
                if (colour != 0 || (k == 0 && t != 0)) continue;  // unused, or the same as t = 0
                colour = colours++;
                define(colour, shade(inks[size_t(t)], back, k * 255 / (kLevels - 1)));
            }
    }

    // One pass over each band sorts every pixel into its colour: bits[k][x] holds which of the
    // band's 6 rows have colour k in column x. used[k] is one past the last such column.
    std::vector<uint8_t> bits(size_t(colours) * c.w);
    for (int y0 = 0; y0 < c.h; y0 += 6) {
        std::fill(bits.begin(), bits.end(), uint8_t(0));
        int used[kMaxColours] = {};
        const int rows = std::min(6, c.h - y0);
        for (int r = 0; r < rows; ++r) {
            const uint8_t* src = &c.px[size_t(y0 + r) * c.w];
            const uint8_t* tones = c.tone.empty() ? nullptr : &c.tone[size_t(y0 + r) * c.w];
            const uint8_t* fills = c.fill.empty() ? nullptr : &c.fill[size_t(y0 + r) * c.w];
            const uint8_t bit = uint8_t(1 << r);
            for (int x = 0; x < c.w; ++x) {
                const int t = tones && tones[x] < kToneCount ? tones[x] : 0;
                const int f = fills && fills[x] < kToneCount ? fills[x] : 0;
                const int k = colourOf[size_t(key(f, t, levelOf[src[x]]))];
                bits[size_t(k) * c.w + x] |= bit;
                if (x >= used[k]) used[k] = x + 1;
            }
        }

        bool first = true;
        for (int k = opaque ? 0 : 1; k < colours; ++k) {  // colour 0 is bare paper: skip it to stay transparent
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

std::string encodeKitty(const Canvas& c, const Inks& inks, RGB paper, bool opaque, bool moveCursor) {
    const int bytesPerPixel = opaque ? 3 : 4;
    std::string pixels(c.px.size() * size_t(bytesPerPixel), '\0');
    for (size_t i = 0; i < c.px.size(); ++i) {
        const RGB ink = inks[c.tone.empty() || c.tone[i] >= kToneCount ? 0 : c.tone[i]];
        const int f = c.fill.empty() || c.fill[i] >= kToneCount ? 0 : c.fill[i];
        // A filled pixel is painted over its tint, even on a transparent background.
        const RGB back = f == 0 ? paper : fillPaper(inks, paper, Tone(f));
        const RGB p = opaque || f != 0 ? shade(ink, back, c.px[i]) : ink;
        char* d = &pixels[i * size_t(bytesPerPixel)];
        d[0] = char(p.r);
        d[1] = char(p.g);
        d[2] = char(p.b);
        if (!opaque) d[3] = char(f != 0 ? 255 : c.px[i]);
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

ToneColours terminalToneColours() { return {"\x1b[39m", "\x1b[32m", "\x1b[33m", "\x1b[31m"}; }

ToneColours toneColours(const Inks& inks, const std::array<bool, kToneCount>& exact) {
    ToneColours codes = terminalToneColours();
    for (int t = 1; t < kToneCount; ++t)  // plain ink is the terminal's own text colour
        if (exact[size_t(t)]) {
            const RGB c = inks[size_t(t)];
            codes[size_t(t)] =
                "\x1b[38;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
        }
    return codes;
}

RGB fillPaper(const Inks& inks, RGB paper, Tone fill) {
    const int t = int(fill);
    return t > 0 && t < kToneCount ? shade(inks[size_t(t)], paper, kFillCoverage) : paper;
}

ToneColours fillColours(const Inks& inks, RGB paper) {
    ToneColours codes{"\x1b[49m", "", "", ""};
    for (int t = 1; t < kToneCount; ++t) {
        const RGB c = fillPaper(inks, paper, Tone(t));
        codes[size_t(t)] =
            "\x1b[48;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m";
    }
    return codes;
}

std::vector<std::string> encodeBraille(const Canvas& c) { return encodeBraille(c, terminalToneColours()); }

std::vector<std::string> encodeBraille(const Canvas& c, const ToneColours& colours) {
    return encodeBraille(c, colours, ToneColours{});
}

std::vector<std::string> encodeBraille(const Canvas& c, const ToneColours& colours, const ToneColours& fills) {
    // Which bit of a braille character raises the dot at (column dx, row dy) of its 2x4 cell.
    static const uint8_t kBit[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
    const bool filling = !c.fill.empty() && !fills[0].empty();
    std::vector<std::string> rows;
    for (int y0 = 0; y0 < c.h; y0 += 4) {
        std::string row;
        row.reserve(size_t(c.w + 1) / 2 * 3);
        Tone current = Tone::Plain;  // the colour the row is in so far
        Tone background = Tone::Plain;
        for (int x0 = 0; x0 < c.w; x0 += 2) {
            int dots = 0;
            int toneDots[kToneCount] = {};
            int fillCells[kToneCount] = {};
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = x0 + dx, y = y0 + dy;
                    if (x >= c.w || y >= c.h) continue;
                    if (filling) {
                        const int f = int(c.fillAt(x, y));
                        ++fillCells[f < kToneCount ? f : 0];
                    }
                    if (c.at(x, y) >= kBrailleThreshold) {
                        dots |= kBit[dy][dx];
                        const int t = int(c.toneAt(x, y));
                        ++toneDots[t < kToneCount ? t : 0];
                    }
                }
            // A character's background is the fill most of it has.
            if (filling) {
                Tone f = Tone::Plain;
                for (int i = 1; i < kToneCount; ++i)
                    if (fillCells[i] > fillCells[int(f)]) f = Tone(i);
                if (f != background) row += fills[int(f)];
                background = f;
            }
            // A character has one colour: the tone of most of its dots. A blank one keeps the
            // colour going, which saves switching back and forth.
            if (dots) {
                Tone t = Tone::Plain;
                for (int i = 1; i < kToneCount; ++i)
                    if (toneDots[i] > toneDots[int(t)]) t = Tone(i);
                if (t != current) row += colours[int(t)];
                current = t;
            }
            const int cp = 0x2800 + dots;  // braille patterns start at U+2800; UTF-8 needs 3 bytes
            row += char(0xE0 | (cp >> 12));
            row += char(0x80 | ((cp >> 6) & 0x3F));
            row += char(0x80 | (cp & 0x3F));
        }
        if (current != Tone::Plain) row += colours[int(Tone::Plain)];
        if (background != Tone::Plain) row += fills[int(Tone::Plain)];
        rows.push_back(std::move(row));
    }
    return rows;
}

bool writePng(const Canvas& c, const Inks& inks, RGB paper, const std::string& path) {
    lunasvg::Bitmap bmp(c.w, c.h);  // 32-bit ARGB in native byte order; opaque, so not premultiplied
    if (bmp.isNull()) return false;
    uint8_t* data = bmp.data();
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            const int t = int(c.toneAt(x, y));
            const RGB back = fillPaper(inks, paper, c.fillAt(x, y));
            const RGB p = shade(inks[t < kToneCount ? t : 0], back, c.at(x, y));
            const uint32_t argb = 0xFF000000u | uint32_t(p.r) << 16 | uint32_t(p.g) << 8 | p.b;
            std::memcpy(data + size_t(y) * bmp.stride() + size_t(x) * 4, &argb, 4);
        }
    }
    return bmp.writeToPng(path);
}
