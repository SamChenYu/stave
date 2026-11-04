#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <string_view>

#include "encode.hpp"
#include "support/sixel_decoder.hpp"

namespace {

constexpr RGB kBlack{0, 0, 0}, kWhite{255, 255, 255};

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

Canvas filled(int w, int h, uint8_t coverage) {
    Canvas c(w, h);
    std::fill(c.px.begin(), c.px.end(), coverage);
    return c;
}

Pixel opaque(RGB c) { return {c.r, c.g, c.b, 255}; }

}  // namespace

// Byte-level checks of small images, worked out by hand from the sixel format. They pin down the
// encoder independently of the test decoder, so the two can't share a misunderstanding.
TEST_SUITE("sixel encoder") {
    TEST_CASE("starts with the sixel header and size, and ends with the terminator") {
        const std::string out = encodeSixel(filled(7, 3, 0), kBlack, kWhite, true);
        CHECK(out.substr(0, 14) == "\x1bP9;1q\"1;1;7;3");
        CHECK(endsWith(out, "\x1b\\"));
    }

    TEST_CASE("a column of solid ink is one character in the darkest colour") {
        // 1x6 is one band; '~' is 63 + 0b111111, all six rows. Colour 7 (black) is the last palette entry.
        CHECK(endsWith(encodeSixel(filled(1, 6, 255), kBlack, kWhite, false), "#7;2;0;0;0#7~\x1b\\"));
    }

    TEST_CASE("long runs are run-length encoded") {
        CHECK(endsWith(encodeSixel(filled(10, 6, 255), kBlack, kWhite, false), "#7;2;0;0;0#7!10~\x1b\\"));
    }

    TEST_CASE("rows past the first six go in a new band after '-'") {
        // 1x7: the second band only has its top row ('@' is 63 + 0b000001).
        CHECK(endsWith(encodeSixel(filled(1, 7, 255), kBlack, kWhite, false), "#7~-#7@\x1b\\"));
    }

    TEST_CASE("opaque mode paints bare paper, transparent mode skips it") {
        Canvas c(2, 6);  // left column ink, right column bare paper
        for (int y = 0; y < 6; ++y) c.at(0, y) = 255;
        // Colour 0 (paper) is empty in column 0 ('?') and full in column 1; '$' returns to the band's start.
        CHECK(endsWith(encodeSixel(c, kBlack, kWhite, true), "#0?~$#7~\x1b\\"));
        CHECK(endsWith(encodeSixel(c, kBlack, kWhite, false), "#7;2;0;0;0#7~\x1b\\"));
    }
}

// Decoding the encoder's output must give back what was drawn.
TEST_SUITE("sixel round trip") {
    TEST_CASE("solid ink and bare paper come back exactly, at sizes that leave partial bands") {
        // Colours sixel can send exactly: it uses whole percentages, and these are multiples of 20%.
        const RGB ink{204, 51, 51}, paper{0, 0, 102};
        std::mt19937 rng(1234);
        for (bool isOpaque : {true, false}) {
            for (int h : {1, 5, 6, 7, 12, 13}) {
                for (int w : {1, 2, 9}) {
                    CAPTURE(isOpaque);
                    CAPTURE(w);
                    CAPTURE(h);
                    Canvas c(w, h);
                    for (uint8_t& p : c.px) p = rng() % 2 ? 255 : 0;

                    const SixelDecodeResult r = decodeSixel(encodeSixel(c, ink, paper, isOpaque));
                    REQUIRE(r.error == "");
                    REQUIRE(r.image.w == w);
                    REQUIRE(r.image.h == h);
                    int wrong = 0;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) {
                            const Pixel want = c.at(x, y) ? opaque(ink) : isOpaque ? opaque(paper) : Pixel{};
                            if (r.image.at(x, y) != want) ++wrong;
                        }
                    CHECK(wrong == 0);
                }
            }
        }
    }

    TEST_CASE("soft edges get 8 shades, getting steadily darker with coverage") {
        Canvas c(256, 1);
        for (int x = 0; x < 256; ++x) c.at(x, 0) = uint8_t(x);
        const SixelDecodeResult r = decodeSixel(encodeSixel(c, kBlack, kWhite, true));
        REQUIRE(r.error == "");
        CHECK(r.image.at(0, 0) == opaque(kWhite));
        CHECK(r.image.at(255, 0) == opaque(kBlack));
        std::set<int> shades;
        for (int x = 0; x < 256; ++x) {
            const Pixel p = r.image.at(x, 0);
            CHECK(p.r == p.g);
            CHECK(p.g == p.b);
            if (x > 0) CHECK(p.r <= r.image.at(x - 1, 0).r);
            shades.insert(p.r);
        }
        CHECK(shades.size() == 8);
    }

    TEST_CASE("marked pixels come back in their tone's ink, and unmarked ones in the plain ink") {
        // Multiples of 20%, which sixel sends exactly.
        const Inks inks{RGB{0, 0, 0}, RGB{0, 204, 51}, RGB{204, 153, 0}, RGB{204, 0, 0}};
        for (bool isOpaque : {true, false}) {
            CAPTURE(isOpaque);
            Canvas c(8, 7);  // two bands
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x) c.at(x, y) = (x + y) % 3 ? 255 : 0;
            c.tone.resize(c.px.size());
            for (size_t i = 0; i < c.tone.size(); ++i) c.tone[i] = uint8_t(i % 4);
            const SixelDecodeResult r = decodeSixel(encodeSixel(c, inks, kWhite, isOpaque));
            REQUIRE(r.error == "");
            int wrong = 0;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x) {
                    const Pixel want = c.at(x, y) ? opaque(inks[size_t(y) * c.w + x & 3]) : isOpaque ? opaque(kWhite) : Pixel{};
                    if (r.image.at(x, y) != want) ++wrong;
                }
            CHECK(wrong == 0);
        }
    }

    TEST_CASE("the palette only has the tones in the picture") {
        const Inks inks{kBlack, RGB{0, 204, 51}, RGB{204, 153, 0}, RGB{204, 0, 0}};
        Canvas c = filled(2, 6, 255);
        c.tone.assign(c.px.size(), uint8_t(Tone::Plain));
        CHECK(encodeSixel(c, inks, kWhite, true) == encodeSixel(filled(2, 6, 255), kBlack, kWhite, true));
        c.tone[0] = uint8_t(Tone::Removed);
        const std::string out = encodeSixel(c, inks, kWhite, true);
        CHECK(out.find("#14;2;") != std::string::npos);   // paper, 7 plain shades and 7 red ones
        CHECK(out.find("#15;2;") == std::string::npos);
        CHECK(out.find("#14;2;80;0;0") != std::string::npos);  // the darkest red is the red ink
    }

    TEST_CASE("an empty canvas is still a valid image") {
        const SixelDecodeResult r = decodeSixel(encodeSixel(Canvas(0, 0), kBlack, kWhite, true));
        CHECK(r.error == "");
        CHECK(r.image.w == 0);
        CHECK(r.image.h == 0);
    }
}

// The decoder is test code too: make sure it catches the mistakes it's meant to catch.
TEST_SUITE("sixel decoder") {
    TEST_CASE("decodes a hand-written image") {
        // 2x2, red top-left and bottom-right on a transparent background:
        // '@' (63 + 0b01) paints row 0 of column 0, 'A' (63 + 0b10) paints row 1 of column 1.
        const SixelDecodeResult r = decodeSixel("\x1bPq\"1;1;2;2#1;2;100;0;0#1@A\x1b\\");
        REQUIRE(r.error == "");
        CHECK(r.image.at(0, 0) == Pixel{255, 0, 0, 255});
        CHECK(r.image.at(1, 0) == Pixel{});
        CHECK(r.image.at(0, 1) == Pixel{});
        CHECK(r.image.at(1, 1) == Pixel{255, 0, 0, 255});
    }

    TEST_CASE("rejects broken input") {
        CHECK(decodeSixel("\x1bPq\"1;1;1;1#0;2;0;0;0#0@").error != "");      // no terminator
        CHECK(decodeSixel("\x1bPq\"1;1;1;6#3~\x1b\\").error != "");           // colour never defined
        CHECK(decodeSixel("\x1bPq\"1;1;1;1#0;2;0;0;0#0~\x1b\\").error != "");  // six rows into a 1-row image
        CHECK(decodeSixel("\x1bPq#0;2;0;0;0#0@\x1b\\").error != "");          // no size
        CHECK(decodeSixel("\x1bPq\"1;1;1;1#0;2;0;0;0#0@ \x1b\\").error != "");  // stray byte
    }
}
