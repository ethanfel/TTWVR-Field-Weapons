// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
struct Vec {
    float x{},y{},z{};
    Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec operator*(float k) const { return {x*k,y*k,z*k}; }
    float length() const { return std::sqrt(x*x+y*y+z*z); }
    bool valid() const { return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z); }
};
namespace motion {
constexpr float gameUnitsPerMetre = 70.f;
// Fallout New Vegas uses this scale for bhkWorldObject and JIP raycast positions.
constexpr float gameUnitsPerHavokUnit = 6.99912548f;
inline Vec toHavokVelocity(Vec metresPerSecond) {
    return metresPerSecond * (gameUnitsPerMetre / gameUnitsPerHavokUnit);
}
inline Vec fromHavokVelocity(Vec havokUnitsPerSecond) {
    return havokUnitsPerSecond * (gameUnitsPerHavokUnit / gameUnitsPerMetre);
}
}
class HandMotion {
    struct Sample { Vec relative; uint32_t when; };
    std::array<Sample,32> samples{};
    unsigned next{}, count{};
public:
    void clear() { count=next=0; }
    Vec step(Vec hand,Vec player,uint32_t now) {
        Vec relative=hand-player;
        if (!relative.valid() || relative.length()>200) { clear(); return {}; }
        if (count) {
            const auto& last=samples[(next+31)%32];
            if (now-last.when>140 || (relative-last.relative).length()>55) clear();
        }
        Vec velocity{};
        for (unsigned i=0;i<count;++i) {
            const auto& old=samples[(next+31-i)%32];
            unsigned ms=now-old.when;
            if (ms>100) break;
            if (ms>=16) velocity=(relative-old.relative)*(1000.f/(motion::gameUnitsPerMetre*ms));
        }
        samples[next]={relative,now}; next=(next+1)%32;
        if (count<32) ++count;
        float speed=velocity.length();
        return speed>12 ? velocity*(12.f/speed) : velocity;
    }
};
