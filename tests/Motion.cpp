// SPDX-License-Identifier: GPL-3.0-only
#include "../src/Motion.h"
#include <cassert>
int main() {
    HandMotion hand;
    Vec player{};
    for (unsigned i=0;i<=90;i+=10) {
        auto velocity=hand.step({30+i*.14f,5,50},player,i);
        if (i>=20) assert(std::abs(velocity.x-2.f)<.001f);
    }
    // Walking with the controller stationary relative to the body adds no throw.
    hand.clear();
    for (unsigned i=0;i<=80;i+=10)
        assert(hand.step({float(i)+20,2,40},{float(i),0,0},i).length()==0);
    assert(hand.step({10,0,40},{},800).length()==0);
    assert(hand.step({140,0,40},{},808).length()==0);
}
