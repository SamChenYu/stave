#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "keys.hpp"

namespace {

// Every key in `input`, in order, until it runs out (or only an incomplete sequence is left).
std::vector<Key> keysIn(std::string input) {
    std::vector<Key> keys;
    for (Key k; (k = takeKey(input)) != Key::None;) keys.push_back(k);
    return keys;
}

using K = std::vector<Key>;

}  // namespace

TEST_SUITE("keys") {
    TEST_CASE("n and N for the next and previous change") {
        std::string in = "nN";
        CHECK(takeKey(in) == Key::NextChange);
        CHECK(takeKey(in) == Key::PreviousChange);
    }

    TEST_CASE("letters and Ctrl+C") {
        CHECK(keysIn("hjklHL gG+-=_qQ\x03") == K{Key::Left, Key::Down, Key::Up, Key::Right, Key::PageLeft,
                                                  Key::PageRight, Key::PageDown, Key::Home, Key::End, Key::ZoomIn,
                                                  Key::ZoomOut, Key::ZoomIn, Key::ZoomOut, Key::Quit, Key::Quit,
                                                  Key::Quit});
        CHECK(keysIn("x") == K{Key::Other});
    }

    TEST_CASE("arrow keys, in both the CSI and SS3 forms terminals send") {
        CHECK(keysIn("\x1b[A\x1b[B\x1b[C\x1b[D") == K{Key::Up, Key::Down, Key::Right, Key::Left});
        CHECK(keysIn("\x1bOA\x1bOB\x1bOC\x1bOD") == K{Key::Up, Key::Down, Key::Right, Key::Left});
    }

    TEST_CASE("Shift+arrows page sideways") {
        CHECK(keysIn("\x1b[1;2C\x1b[1;2D") == K{Key::PageRight, Key::PageLeft});
    }

    TEST_CASE("PgUp, PgDn, and the several ways Home and End arrive") {
        CHECK(keysIn("\x1b[5~\x1b[6~") == K{Key::PageUp, Key::PageDown});
        CHECK(keysIn("\x1b[H\x1b[1~\x1b[7~\x1bOH") == K{Key::Home, Key::Home, Key::Home, Key::Home});
        CHECK(keysIn("\x1b[F\x1b[4~\x1b[8~\x1bOF") == K{Key::End, Key::End, Key::End, Key::End});
    }

    TEST_CASE("unused sequences are consumed whole") {
        std::string in = "\x1b[15~j";  // F5, then j
        CHECK(takeKey(in) == Key::Other);
        CHECK(takeKey(in) == Key::Down);
        in = "\x1bxj";  // Alt+x, then j
        CHECK(takeKey(in) == Key::Other);
        CHECK(takeKey(in) == Key::Down);
    }

    TEST_CASE("a sequence split across reads waits for the rest") {
        std::string in = "\x1b";
        CHECK(takeKey(in) == Key::None);
        in += "[";
        CHECK(takeKey(in) == Key::None);
        in += "5";
        CHECK(takeKey(in) == Key::None);
        in += "~";
        CHECK(takeKey(in) == Key::PageUp);
        CHECK(in.empty());
    }

    TEST_CASE("Esc on its own quits once no more input follows") {
        std::string in = "\x1b";
        CHECK(takeKey(in) == Key::None);
        CHECK(flushPending(in) == Key::Quit);
        CHECK(in.empty());
    }

    TEST_CASE("Esc pressed twice quits straight away") {
        std::string in = "\x1b\x1b";
        CHECK(takeKey(in) == Key::Quit);
    }

    TEST_CASE("an incomplete sequence left over is dropped, not taken as Esc") {
        std::string in = "\x1b[1;";
        CHECK(takeKey(in) == Key::None);
        CHECK(flushPending(in) == Key::None);
        CHECK(in.empty());
    }
}
