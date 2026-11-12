// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <lib.hpp>
#include "totk/port/CodeCheck.hpp"
#include "BombInstall.hpp"

extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    if (!totk::port::start("remote-bomb")) return;
    remote_bomb::install(exl::util::modules::GetTargetStart());
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
