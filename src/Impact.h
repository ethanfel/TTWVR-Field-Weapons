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

// Compare collision volumes in the controller's own units. These reference
// dimensions were measured on an adult raider in the New Vegas VR runtime.
inline float relativeBulk(float radius, float height) {
    if (!std::isfinite(radius) || !std::isfinite(height) || radius<=0 || height<=0) return 0;
    float widthRatio=radius/2.89f;
    return std::clamp(widthRatio*widthRatio*(height/18.29f),.25f,32.f);
}
inline bool staggers(float weight, float speed, float bulk) {
    if (!std::isfinite(weight) || !std::isfinite(speed) || !std::isfinite(bulk) ||
        weight<=0 || speed<=minimumSpeed || bulk<=0) return false;
    return weight*(speed-minimumSpeed) >= 2.7f*std::pow(bulk,.75f);
}
}
