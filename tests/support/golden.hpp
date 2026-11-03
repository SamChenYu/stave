#pragma once
#include <string>

#include "support/image.hpp"

// Checks `actual` against the expected image tests/golden/<name>.png, failing the current test
// if they differ. On a failure it saves <name>.actual.png and <name>.diff.png (the expected image
// faded, with differing pixels in red) to the test output folder, and the failure message says where.
//
// Run with STAVE_UPDATE_GOLDEN=1 set to write `actual` as the new expected image instead; look at
// the new PNGs before committing them, since they become the definition of correct.
//
// tolerance is the largest per-channel difference that still counts as equal.
void checkGolden(const std::string& name, const Image& actual, int tolerance = 0);
