#pragma once
#include "canvas.hpp"

// A 384x96 image for checking graphics output. Top half: a ramp from bare paper to solid ink,
// showing every shade the encoder has. Bottom half: a checkerboard (hard edges) next to a disc
// (soft, anti-aliased edges).
Canvas testPattern();
