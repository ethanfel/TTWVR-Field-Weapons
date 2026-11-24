// SPDX-License-Identifier: GPL-3.0-only
#include "../src/Motion.h"
#include "../src/Impact.h"
#include <cassert>
int main() {
    assert(impact::damage(12,0)==0);
    assert(impact::damage(3,impact::minimumSpeed)==0);
    assert(impact::damage(0,5)==0 && impact::damage(-1,5)==0);
    assert(impact::damage(3,NAN)==0 && impact::damage(INFINITY,5)==0);
    assert(impact::damage(3,3)>0 && impact::damage(3,3)<impact::damage(6,3));
    assert(impact::damage(6,3)<impact::damage(6,5));
    assert(impact::damage(10000,12)==impact::maximumDamage);
    const float raider=impact::relativeBulk(2.89f,18.29f);
    const float mutant=impact::relativeBulk(6.02f,21.83f);
    assert(impact::staggers(3,2.4f,raider));
    assert(!impact::staggers(3,2.4f,mutant));
    assert(impact::staggers(12,2.4f,mutant));
    assert(!impact::staggers(12,impact::minimumSpeed,raider));
    assert(!impact::staggers(3,2.4f,impact::relativeBulk(0,18)));
    assert(!impact::staggers(NAN,2.4f,raider));
    assert(!impact::staggers(12,2.4f,impact::relativeBulk(15.f,48.f)));
    HandMotion hand;
    Vec player{};
    for (unsigned i=0;i<=90;i+=10) {
        auto velocity=hand.step({30+i*.14f,5,50},player,i);
        if (i>=20) assert(std::abs(velocity.x-2.f)<.001f);
    }
    // Replay 60 ms of hand travel and integrate one Havok step in game space.
    hand.clear();
    const Vec displacement{21,-14,7};
    hand.step({0,0,30},player,100);
    const Vec measured=hand.step({21,-14,37},player,160);
    const Vec native=motion::toHavokVelocity(measured);
    assert((native*(.060f*6.99912548f)-displacement).length()<.001f);
    const Vec incoming=motion::fromHavokVelocity(native);
    assert((incoming-measured).length()<.001f);
    assert(std::abs(impact::damage(3,incoming.length())-impact::damage(3,measured.length()))<.001f);
    assert(motion::toHavokVelocity({}).length()==0);
    // Walking with the controller stationary relative to the body adds no throw.
    hand.clear();
    for (unsigned i=0;i<=80;i+=10)
        assert(hand.step({float(i)+20,2,40},{float(i),0,0},i).length()==0);
    assert(hand.step({10,0,40},{},800).length()==0);
    assert(hand.step({140,0,40},{},808).length()==0);
}
