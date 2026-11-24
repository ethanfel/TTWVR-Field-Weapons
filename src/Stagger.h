// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Api.h"

namespace stagger {
unsigned initialize(Scripts* scripts, Arrays* arrays);
float targetBulk(unsigned char* actor);
unsigned play(unsigned char* actor);
}
