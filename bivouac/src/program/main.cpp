// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <lib.hpp>

#include "runtime/BivouacRuntime.hpp"

extern "C" void exl_main(void* /*x0*/, void* /*x1*/) {
    bivouac::runtime::install();
}

// Referenced by exlaunch's crt0. This module never runs as a process.
extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("unreachable");
}
