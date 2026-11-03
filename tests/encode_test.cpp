#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>  // lets doctest print std::string_view
#include <string>
#include <string_view>
#include <vector>

#include "encode.hpp"

namespace {

constexpr RGB kInk{204, 51, 51}, kPaper{0, 0, 102};

std::string base64Decode(std::string_view in) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int bits = 0, buffer = 0;
    for (char c : in) {
        const int v = value(c);
        if (v < 0) continue;  // '=' padding
        buffer = buffer << 6 | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

// A kitty image split back into its chunks: the keys of the first, and all the data joined up.
struct KittyImage {
    std::string firstKeys;  // e.g. "a=T,f=32,q=2,C=1,s=3,v=2,m=0"
    std::string pixels;     // decoded
    std::vector<size_t> chunkSizes;
    bool lastSaysDone = false;
    bool othersSayMore = true;
};

KittyImage parseKitty(std::string_view s) {
    KittyImage img;
    std::string data;
    for (size_t at = 0; at < s.size();) {
        REQUIRE(s.substr(at, 3) == "\x1b_G");
        const size_t semi = s.find(';', at), end = s.find("\x1b\\", at);
        REQUIRE(semi < end);
        const std::string_view keys = s.substr(at + 3, semi - at - 3);
        if (img.chunkSizes.empty()) img.firstKeys = std::string(keys);
        const bool more = keys.find("m=1") != std::string_view::npos;
        const size_t next = end + 2;
        if (next < s.size() && !more) img.othersSayMore = false;
        if (next >= s.size()) img.lastSaysDone = keys.find("m=0") != std::string_view::npos;
        img.chunkSizes.push_back(end - semi - 1);
        data += s.substr(semi + 1, end - semi - 1);
        at = next;
    }
    img.pixels = base64Decode(data);
    return img;
}

}  // namespace

TEST_SUITE("kitty encoder") {
    TEST_CASE("transparent: every pixel is ink, with alpha = coverage") {
        Canvas c(3, 2);
        c.px = {0, 64, 255, 128, 1, 200};
        const KittyImage img = parseKitty(encodeKitty(c, kInk, kPaper, /*opaque=*/false, /*moveCursor=*/false));
        CHECK(img.firstKeys.find("a=T,f=32,q=2,C=1,s=3,v=2,") == 0);
        REQUIRE(img.pixels.size() == 6 * 4);
        for (size_t i = 0; i < 6; ++i) {
            CHECK(uint8_t(img.pixels[i * 4]) == kInk.r);
            CHECK(uint8_t(img.pixels[i * 4 + 1]) == kInk.g);
            CHECK(uint8_t(img.pixels[i * 4 + 2]) == kInk.b);
            CHECK(uint8_t(img.pixels[i * 4 + 3]) == c.px[i]);
        }
    }

    TEST_CASE("opaque: RGB blended from paper to ink") {
        Canvas c(2, 1);
        c.px = {0, 255};
        const KittyImage img = parseKitty(encodeKitty(c, kInk, kPaper, /*opaque=*/true, /*moveCursor=*/false));
        CHECK(img.firstKeys.find("f=24") != std::string::npos);
        REQUIRE(img.pixels.size() == 2 * 3);
        CHECK(img.pixels == std::string{char(kPaper.r), char(kPaper.g), char(kPaper.b), char(kInk.r), char(kInk.g),
                                        char(kInk.b)});
    }

    TEST_CASE("big images go in chunks of at most 4096, each saying whether more follow") {
        Canvas c(100, 50);  // 20,000 bytes of RGBA: 26,668 of base64
        for (size_t i = 0; i < c.px.size(); ++i) c.px[i] = uint8_t(i * 7);
        const KittyImage img = parseKitty(encodeKitty(c, kInk, kPaper, false, false));
        CHECK(img.chunkSizes.size() == 7);
        for (size_t n : img.chunkSizes) CHECK(n <= 4096);
        CHECK(img.othersSayMore);
        CHECK(img.lastSaysDone);
        REQUIRE(img.pixels.size() == c.px.size() * 4);
        int wrong = 0;
        for (size_t i = 0; i < c.px.size(); ++i) wrong += uint8_t(img.pixels[i * 4 + 3]) != c.px[i];
        CHECK(wrong == 0);
    }

    TEST_CASE("for printing, the cursor moves below the image") {
        const std::string out = encodeKitty(Canvas(1, 1), kInk, kPaper, false, /*moveCursor=*/true);
        CHECK(out.find("C=1") == std::string::npos);
    }
}

TEST_SUITE("braille encoder") {
    TEST_CASE("each character is a 2x4 cell of dots") {
        Canvas full(2, 4);
        std::fill(full.px.begin(), full.px.end(), uint8_t(255));
        CHECK(encodeBraille(full) == std::vector<std::string>{"⣿"});  // all eight dots
        CHECK(encodeBraille(Canvas(2, 4)) == std::vector<std::string>{"⠀"});  // none

        Canvas corners(2, 4);
        corners.at(0, 0) = 255;  // dot 1
        corners.at(1, 3) = 255;  // dot 8
        CHECK(encodeBraille(corners) == std::vector<std::string>{"⢁"});
    }

    TEST_CASE("thin lines show: a dot is raised from a sixth covered") {
        Canvas c(4, 4);
        c.at(0, 0) = kBrailleThreshold;
        c.at(2, 0) = kBrailleThreshold - 1;
        CHECK(encodeBraille(c) == std::vector<std::string>{"⠁⠀"});
    }

    TEST_CASE("sizes that don't fill whole cells still get a character for the remainder") {
        Canvas c(3, 5);  // 2 characters across, 2 rows
        c.at(2, 4) = 255;  // the left column, top row of the bottom-right cell
        const std::vector<std::string> rows = encodeBraille(c);
        REQUIRE(rows.size() == 2);
        CHECK(rows[0] == "⠀⠀");
        CHECK(rows[1] == "⠀⠁");
    }
}
