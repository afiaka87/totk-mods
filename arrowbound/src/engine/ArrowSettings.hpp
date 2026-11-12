// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstddef>

namespace arrowbound::settings {
bool enabled();
void request(bool enabled);
void service();
bool writeFlightStatus(const char* data, std::size_t size);
}
