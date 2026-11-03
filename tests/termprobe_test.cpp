#include <doctest/doctest.h>

#include <string>

#include "termprobe.hpp"

namespace {

bool operator==(RGB a, RGB b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// What the replies to kTerminalQueries look like from a terminal that answers everything
// (cell 9x19 pixels, light grey text on a near-black background), with ST-terminated colours.
const std::string kAllReplies =
    "\x1b[6;19;9t"
    "\x1b[4;570;1080t"
    "\x1b]10;rgb:cccc/cccc/cccc\x1b\\"
    "\x1b]11;rgb:0c0c/0c0c/0c0c\x1b\\"
    "\x1b[?61;4;6;7;14;21;22;23;24;28;32;42c";

}  // namespace

TEST_SUITE("terminal replies") {
    TEST_CASE("a terminal that answers everything") {
        const TerminalReplies r = parseTerminalReplies(kAllReplies);
        CHECK(r.complete);
        CHECK(r.cellW == 9);
        CHECK(r.cellH == 19);
        CHECK(r.windowW == 1080);
        CHECK(r.windowH == 570);
        REQUIRE(r.fg);
        CHECK(*r.fg == RGB{204, 204, 204});
        REQUIRE(r.bg);
        CHECK(*r.bg == RGB{12, 12, 12});
    }

    TEST_CASE("a terminal that only answers device attributes") {
        const TerminalReplies r = parseTerminalReplies("\x1b[?1;2c");
        CHECK(r.complete);
        CHECK(r.cellW == 0);
        CHECK(!r.fg);
        CHECK(!r.bg);
    }

    TEST_CASE("nothing is complete until the device-attributes reply ends") {
        CHECK(!parseTerminalReplies("").complete);
        CHECK(!parseTerminalReplies("\x1b[6;19;9t\x1b[?61;4").complete);  // cut off mid-reply
    }

    TEST_CASE("colour replies may end with BEL and use 1 to 4 hex digits per component") {
        const TerminalReplies r = parseTerminalReplies("\x1b]10;rgb:f/8/0\x07\x1b]11;rgb:1e/fff/800\x07");
        REQUIRE(r.fg);
        CHECK(*r.fg == RGB{255, 136, 0});  // f/15, 8/15, 0
        REQUIRE(r.bg);
        CHECK(*r.bg == RGB{30, 255, 128});  // 0x1e, fff/4095, 800/4095
    }

    TEST_CASE("malformed replies are ignored") {
        const TerminalReplies r =
            parseTerminalReplies("\x1b]10;rgb:zz/00/00\x1b\\\x1b]11;rgb:00000/0/0\x1b\\\x1b[6;0;9t\x1b[6;19t");
        CHECK(!r.fg);       // not hex
        CHECK(!r.bg);       // too many digits
        CHECK(r.cellW == 0);  // a zero size, then a reply with a number missing
    }

    TEST_CASE("replies are found among other input, like a key pressed meanwhile") {
        const TerminalReplies r = parseTerminalReplies("q" + kAllReplies + "x");
        CHECK(r.complete);
        CHECK(r.cellW == 9);
        CHECK(r.fg);
    }
}

TEST_SUITE("graphics support") {
    TEST_CASE("a 4 among the device attributes means sixel") {
        CHECK(parseTerminalReplies("\x1b[?61;4;6;7c").sixel);
        CHECK(!parseTerminalReplies("\x1b[?61;6;7c").sixel);
        CHECK(!parseTerminalReplies("\x1b[?14;42c").sixel);  // 14 and 42 aren't 4
        CHECK(parseTerminalReplies("\x1b[?4c").sixel);
    }

    TEST_CASE("an OK to the kitty query means kitty graphics") {
        CHECK(parseTerminalReplies("\x1b_Gi=31;OK\x1b\\\x1b[?62c").kitty);
        CHECK(!parseTerminalReplies("\x1b_Gi=31;ENOTSUPPORTED:x\x1b\\\x1b[?62c").kitty);
        CHECK(!parseTerminalReplies("\x1b[?62c").kitty);
    }

    TEST_CASE("the best output: kitty, then sixel, then braille") {
        TerminalInfo t;
        t.answered = true;
        t.kitty = t.sixel = true;
        CHECK(chooseOutput(t) == Output::Kitty);
        t.kitty = false;
        CHECK(chooseOutput(t) == Output::Sixel);
        t.sixel = false;
        CHECK(chooseOutput(t) == Output::Braille);
    }

    TEST_CASE("no answer at all (or not a terminal) keeps sixel") {
        CHECK(chooseOutput(TerminalInfo{}) == Output::Sixel);
    }

    TEST_CASE("the replies are carried into the terminal info") {
        const TerminalInfo t = combineTerminalInfo({80, 24, 0, 0}, parseTerminalReplies("\x1b_Gi=31;OK\x1b\\\x1b[?4c"));
        CHECK(t.answered);
        CHECK(t.kitty);
        CHECK(t.sixel);
    }
}

TEST_SUITE("terminal info") {
    TEST_CASE("the terminal's cell size is used first") {
        const TerminalInfo t = combineTerminalInfo({120, 30, 0, 0}, parseTerminalReplies(kAllReplies));
        CHECK(t.cellW == 9);
        CHECK(t.pixelWidth() == 1080);
    }

    TEST_CASE("without a cell size, the window size in pixels is divided by the character size") {
        const TerminalInfo t = combineTerminalInfo({100, 25, 0, 0}, parseTerminalReplies("\x1b[4;500;800t\x1b[?1c"));
        CHECK(t.cellW == 8);
        CHECK(t.cellH == 20);
        CHECK(t.pixelWidth() == 800);
    }

    TEST_CASE("the OS's pixel size is the last resort") {
        const TerminalInfo t = combineTerminalInfo({80, 24, 640, 384}, parseTerminalReplies("\x1b[?1c"));
        CHECK(t.cellW == 8);
        CHECK(t.cellH == 16);
    }

    TEST_CASE("with no pixel sizes at all, the width is unknown") {
        const TerminalInfo t = combineTerminalInfo({80, 24, 0, 0}, parseTerminalReplies("\x1b[?1c"));
        CHECK(t.pixelWidth() == 0);
    }
}

TEST_SUITE("colours") {
    TEST_CASE("with both terminal colours known, ink is the text colour on a transparent background") {
        TerminalInfo t;
        t.fg = RGB{204, 204, 204};
        t.bg = RGB{12, 12, 12};
        const InkAndPaper c = chooseColours(t);
        CHECK(c.ink == RGB{204, 204, 204});
        CHECK(c.paper == RGB{12, 12, 12});
        CHECK(!c.opaque);
    }

    TEST_CASE("with either colour unknown, black ink on a white page") {
        TerminalInfo t;
        t.bg = RGB{12, 12, 12};
        const InkAndPaper c = chooseColours(t);
        CHECK(c.ink == RGB{0, 0, 0});
        CHECK(c.paper == RGB{255, 255, 255});
        CHECK(c.opaque);
    }
}
