#include <doctest/doctest.h>

#include <array>
#include <ostream>  // lets doctest print std::string
#include <string>
#include <vector>

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
    "\x1b]4;1;rgb:cdcd/3131/3131\x1b\\"
    "\x1b]4;2;rgb:0d0d/bcbc/7979\x1b\\"
    "\x1b]4;3;rgb:e5e5/e5e5/1010\x1b\\"
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
        REQUIRE(r.red);
        CHECK(*r.red == RGB{205, 49, 49});
        REQUIRE(r.green);
        CHECK(*r.green == RGB{13, 188, 121});
        REQUIRE(r.yellow);
        CHECK(*r.yellow == RGB{229, 229, 16});
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

    TEST_CASE("the best output: sixel, then braille (kitty is disabled for now)") {
        TerminalInfo t;
        t.answered = true;
        // Kitty is never chosen automatically, even when the terminal supports it.
        t.kitty = t.sixel = true;
        CHECK(chooseOutput(t) == Output::Sixel);
        t.sixel = false;
        CHECK(chooseOutput(t) == Output::Braille);
        t.sixel = true;
        CHECK(chooseOutput(t) == Output::Sixel);
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

    TEST_CASE("on a HiDPI display the cell grows to the device pixels an image draws in") {
        // iTerm2 on a 2x display: it answers CSI 14 t in logical points (text area 1536 wide over
        // 190 columns), but the OS reports the real 3040 device pixels, so one cell is 16 not 8.
        const TerminalInfo t = combineTerminalInfo({190, 54, 3040, 1836}, parseTerminalReplies("\x1b[4;924;1536t\x1b[?4c"));
        CHECK(t.cellW == 16);
        CHECK(t.pixelWidth() == 3040);  // a full-width image spans the whole window
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

    TEST_CASE("marked bars are in the theme's green, yellow and red") {
        TerminalInfo t;
        t.fg = RGB{204, 204, 204};
        t.bg = RGB{12, 12, 12};
        t.red = RGB{205, 49, 49};
        t.green = RGB{13, 188, 121};
        t.yellow = RGB{229, 229, 16};
        const InkAndPaper c = chooseColours(t);
        CHECK(c.added == RGB{13, 188, 121});
        CHECK(c.changed == RGB{229, 229, 16});
        CHECK(c.removed == RGB{205, 49, 49});
        const Inks inks = c.inks();
        CHECK(inks[size_t(Tone::Plain)] == RGB{204, 204, 204});
        CHECK(inks[size_t(Tone::Removed)] == RGB{205, 49, 49});
    }

    TEST_CASE("without the theme's colours, marks are in colours that suit the background") {
        TerminalInfo dark;
        dark.fg = RGB{204, 204, 204};
        dark.bg = RGB{12, 12, 12};
        TerminalInfo light;
        light.fg = RGB{30, 30, 30};
        light.bg = RGB{250, 250, 250};
        const InkAndPaper d = chooseColours(dark), l = chooseColours(light), unknown = chooseColours(TerminalInfo{});
        CHECK(d.removed.r > l.removed.r);  // lighter on a dark background
        CHECK(d.added.g > l.added.g);
        CHECK(unknown.removed == l.removed);  // black on white is a light background
    }

    TEST_CASE("an ordinary theme's colours are used as they are, with nothing to explain") {
        TerminalInfo t;
        t.fg = RGB{204, 204, 204};
        t.bg = RGB{12, 12, 12};
        t.red = RGB{205, 49, 49};
        t.green = RGB{13, 188, 121};
        t.yellow = RGB{229, 229, 16};
        std::vector<std::string> notes;
        const InkAndPaper c = chooseColours(t, &notes);
        CHECK(c.added == RGB{13, 188, 121});
        CHECK(notes.empty());
        CHECK(c.exact == std::array<bool, kToneCount>{});
        // ...and so are stave's own, on dark and light backgrounds and on a page.
        t.red = t.green = t.yellow = std::nullopt;
        chooseColours(t, &notes);
        t.fg = RGB{30, 30, 30};
        t.bg = RGB{250, 250, 250};
        chooseColours(t, &notes);
        chooseColours(TerminalInfo{}, &notes);
        CHECK(notes.empty());
    }

    TEST_CASE("on green text, added bars aren't green") {
        // Lime-green text on black, with a theme green much like it.
        TerminalInfo t;
        t.fg = RGB{151, 245, 104};
        t.bg = RGB{0, 0, 0};
        t.green = RGB{110, 240, 80};
        t.yellow = RGB{240, 200, 60};
        t.red = RGB{255, 90, 90};
        std::vector<std::string> notes;
        const InkAndPaper c = chooseColours(t, &notes);
        CHECK(c.added == RGB{90, 170, 255});  // stave's green is too green as well: the blue backup
        CHECK(c.changed == RGB{240, 200, 60});
        CHECK(c.removed == RGB{255, 90, 90});
        CHECK(c.exact == std::array<bool, kToneCount>{false, true, false, false});
        REQUIRE(notes.size() == 1);
        CHECK(notes[0] ==
              "added #5aaaff: the theme's green #6ef050 is too like the text colour, and stave's usual green "
              "#5fc873 is too like the text colour");
    }

    TEST_CASE("a mark too faint on the background is replaced") {
        TerminalInfo t;
        t.fg = RGB{30, 30, 30};
        t.bg = RGB{250, 250, 250};
        t.yellow = RGB{250, 240, 120};  // pale yellow on white
        std::vector<std::string> notes;
        const InkAndPaper c = chooseColours(t, &notes);
        CHECK(c.changed == RGB{175, 110, 0});
        CHECK(c.exact[size_t(Tone::Changed)]);
        REQUIRE(notes.size() == 1);
        CHECK(notes[0] == "changed #af6e00: the theme's yellow #faf078 is too faint on the background");
    }

    TEST_CASE("marks are kept apart from each other") {
        TerminalInfo t;
        t.fg = RGB{204, 204, 204};
        t.bg = RGB{12, 12, 12};
        t.green = RGB{200, 200, 60};  // a yellowish green...
        t.yellow = RGB{215, 195, 50};  // ...and a yellow much like it
        std::vector<std::string> notes;
        const InkAndPaper c = chooseColours(t, &notes);
        CHECK(c.added == RGB{200, 200, 60});
        CHECK(c.changed == RGB{255, 150, 40});  // stave's yellow is too like it as well: the orange backup
        REQUIRE(notes.size() == 1);
        CHECK(notes[0].find("too like the added colour") != std::string::npos);
    }

    TEST_CASE("a theme of greys gets stave's own colours") {
        TerminalInfo t;
        t.fg = RGB{204, 204, 204};
        t.bg = RGB{12, 12, 12};
        t.red = t.green = t.yellow = RGB{180, 180, 180};
        const InkAndPaper c = chooseColours(t);
        CHECK(c.added == RGB{95, 200, 115});
        CHECK(c.changed == RGB{230, 190, 70});
        CHECK(c.removed == RGB{240, 95, 95});
        CHECK(c.exact == std::array<bool, kToneCount>{false, true, true, true});
    }

    TEST_CASE("the contrast ratio and colour distance") {
        CHECK(contrastRatio({0, 0, 0}, {255, 255, 255}) == doctest::Approx(21));
        CHECK(contrastRatio({90, 90, 90}, {90, 90, 90}) == doctest::Approx(1));
        CHECK(markColourDistance({0, 0, 0}, {255, 255, 255}) == doctest::Approx(0).epsilon(1e-6));  // greys
        CHECK(markColourDistance({255, 0, 0}, {0, 255, 0}) > 0.4);
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
