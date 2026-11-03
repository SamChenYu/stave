#include <lunasvg.h>

#include <cstdio>
#include <string_view>

#include "encode.hpp"
#include "terminal.hpp"
#include "testpattern.hpp"

namespace {

const char* const kUsage =
    "usage: stave [--version] [--test-pattern]\n"
    "  --version       print the versions of stave and its libraries\n"
    "  --test-pattern  draw a test image, to check that the terminal can show sixel graphics\n";

}  // namespace

int main(int argc, char** argv) {
    bool testPatternRequested = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--version") {
            std::printf("stave %s\nlunasvg %s\n", STAVE_VERSION, lunasvg_version_string());
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (arg == "--test-pattern") {
            testPatternRequested = true;
        } else {
            std::fprintf(stderr, "stave: unknown option '%s'\n\n%s", argv[i], kUsage);
            return 2;
        }
    }
    if (!testPatternRequested) {
        std::fputs(kUsage, stdout);
        return 0;
    }

    ConsoleSetup console;
    const RGB ink{0, 0, 0}, paper{255, 255, 255};  // until we can ask the terminal for its colours
    if (!writeAll(encodeSixel(testPattern(), ink, paper, /*opaque=*/true) + "\n")) {
        std::fputs("stave: couldn't write to stdout\n", stderr);
        return 1;
    }
    return 0;
}
