// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Motion.h"
namespace host {
using Ptr = unsigned char*;
struct Input { bool tracked, grip, keep; };
bool connect();
const char* version();
bool observeHolsters();
int recentHolster(int hand);
void forgetHolsters();
Input input(int hand);
Ptr held(int hand);
bool pose(int hand,Vec& point,Vec& angles);
bool take(int hand);
bool draw(int hand, Ptr weapon);
int drawHand();
}
