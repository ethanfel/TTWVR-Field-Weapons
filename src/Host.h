// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Motion.h"
namespace host {
using Ptr = unsigned char*;
struct Input { bool tracked, grip, keep; };
// TTWVR 1.1.8 uses this value type when transferring a gun between hands.
struct ReloadState {
    int live{};
    unsigned char magazine{1}, padding[3]{};
    int magazineRounds{};
    unsigned char chamber{}, slideLocked{}, slideForward{}, triggerLatch{};
    float action{};
    unsigned char actionFlags[4]{1,1,1,0};
    int spent{};
    unsigned char chambers[16]{};
    int chamberIndex{};
    unsigned char openFlags[4]{};

    // TTWVR marks a detachable magazine as absent during ejection. A saved
    // weapon can then retain at most its chambered round.
    bool reconcileMagazine() {
        if (magazine || live<=1) return false;
        magazine=1;
        magazineRounds=live-(chamber ? 1 : 0);
        return true;
    }
};
static_assert(sizeof(ReloadState)==0x34);
bool readReload(Ptr gun,ReloadState& state);
bool rememberReload(Ptr gun,Ptr ammo,const ReloadState& state,int numberedSlot=0);
bool rememberEmptyHolster(Ptr gun,Ptr ammo,int numberedSlot,const ReloadState& state);
void applyReload(Ptr player,Ptr gun);
bool connect();
const char* version();
bool observeReloadSlots(int (*slotForWornCopy)(Ptr));
bool observeHolsters();
bool observePouchReturns();
unsigned takePouchReturn(unsigned kind);
int recentHolster(int hand);
int holsterAt(Ptr player,int hand);
void forgetHolsters();
Input input(int hand);
Ptr held(int hand);
bool pose(int hand,Vec& point,Vec& angles);
bool take(int hand);
bool draw(int hand, Ptr weapon);
int drawHand();
}
