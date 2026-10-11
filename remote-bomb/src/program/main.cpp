// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <lib.hpp>
#include "BombInstall.hpp"

extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    remote_bomb::install(exl::util::modules::GetTargetStart());
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
