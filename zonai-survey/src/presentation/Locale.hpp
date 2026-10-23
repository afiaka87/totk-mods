// SPDX-License-Identifier: MIT
#pragma once
#include "LocalePack.hpp"
namespace zonai_survey::locale {
void loadOnce();
bool ready();
const Pack& active();
unsigned char* storage();
}
