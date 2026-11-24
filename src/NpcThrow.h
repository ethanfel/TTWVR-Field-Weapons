// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Api.h"
#include "Game.h"
#include <cstdint>

namespace npc {
using Released = void (*)(game::Ptr actor, game::Ptr item, game::Ptr ammo, Vec velocity);
bool initialize(Scripts* api);
void reset();
void update(game::Ptr player, uint32_t now, Released released);
}
