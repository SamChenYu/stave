#pragma once
#include <array>
#include <cstdint>

struct RGB {
    uint8_t r, g, b;
};

// What a stretch of ink is marked as, for showing a diff (--added, --changed, --removed). Plain is
// unmarked ink, in the usual ink colour.
enum class Tone : uint8_t { Plain, Added, Changed, Removed };
constexpr int kToneCount = 4;

// An ink colour for each tone, indexed by Tone.
using Inks = std::array<RGB, kToneCount>;

// The same ink for every tone, so marks don't show.
constexpr Inks oneInk(RGB ink) { return {ink, ink, ink, ink}; }
