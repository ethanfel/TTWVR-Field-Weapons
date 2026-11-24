// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace impact {
constexpr float minimumSpeed = 1.25f;  // Measured hand/released body speed.
constexpr float maximumDamage = 35.f;

// Carry weight is in Fallout inventory units. This is a gameplay curve.
inline float damage(float weight, float incomingSpeed) {
    if (!std::isfinite(weight) || !std::isfinite(incomingSpeed) ||
        weight <= 0 || incomingSpeed <= minimumSpeed) return 0;
    return std::min(maximumDamage, 2.f * std::sqrt(weight) * (incomingSpeed - minimumSpeed));
}
}
