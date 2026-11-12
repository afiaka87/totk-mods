// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <lib.hpp>
#include "totk/port/CodeCheck.hpp"
#include "CryonisInstall.hpp"

extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    if (!totk::port::start("cryonis")) return;
    cryonis::install(exl::util::modules::GetTargetStart());
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
