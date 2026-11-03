#include "support/golden.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

namespace {

bool updateRequested() {
    const char* v = std::getenv("STAVE_UPDATE_GOLDEN");
    return v && *v && std::string_view(v) != "0";
}

}  // namespace

void checkGolden(const std::string& name, const Image& actual, int tolerance) {
    const fs::path expectedPath = (fs::path(STAVE_GOLDEN_DIR) / (name + ".png")).make_preferred();
    const fs::path outDir = fs::path(STAVE_TEST_OUTPUT_DIR).make_preferred();
    const std::string actualPath = (outDir / (name + ".actual.png")).string();
    const std::string diffPath = (outDir / (name + ".diff.png")).string();
    std::error_code ec;
    fs::remove(actualPath, ec);  // clear output left over from an earlier failing run
    fs::remove(diffPath, ec);

    if (updateRequested()) {
        fs::create_directories(expectedPath.parent_path());
        REQUIRE_MESSAGE(savePng(actual, expectedPath.string()), "couldn't write " << expectedPath.string());
        MESSAGE("updated " << expectedPath.string());
        return;
    }

    fs::create_directories(outDir);
    const std::optional<Image> expected = loadPng(expectedPath.string());
    if (!expected) {
        savePng(actual, actualPath);
        FAIL_CHECK("no expected image at " << expectedPath.string() << ". This run's output is in " << actualPath
                                           << ": check it, then run with STAVE_UPDATE_GOLDEN=1 to accept it.");
        return;
    }

    const ImageDiff diff = compareImages(actual, *expected, tolerance);
    if (!diff.sameSize) {
        savePng(actual, actualPath);
        FAIL_CHECK(name << ": size is " << actual.w << "x" << actual.h << ", expected " << expected->w << "x"
                        << expected->h << ". This run's output is in " << actualPath);
        return;
    }
    if (diff.differing == 0) return;

    savePng(actual, actualPath);
    savePng(diff.visual, diffPath);
    FAIL_CHECK(name << ": " << diff.differing << " of " << expected->px.size()
                    << " pixels differ (largest channel difference " << diff.maxDelta << ", tolerance " << tolerance
                    << ").\n  actual: " << actualPath << "\n  diff:   " << diffPath);
}
