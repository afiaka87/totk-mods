// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

#include <lib.hpp>

#include "runtime/BivouacRuntime.hpp"
#include "totk/port/CodeCheck.hpp"

#ifdef TOTK_SUITE_MEMBER
#include "totk/suite/Member.hpp"
#endif

namespace bivouac::suite {

// Shared by the standalone module and Zonai Fieldworks. Game places are named by their 1.2.1
// offset and resolved for the running game (1.2.1 or 1.4.3); any other game is refused.
bool install(std::uintptr_t /*mainBase*/) {
    if (!totk::port::supported()) return false;
    bivouac::runtime::install();
    return true;
}

#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks' switch. Off waits while a camp warp's arrival is watched, so Link is still lifted
// onto the deck; then the camps leave the world until Bivouac is switched on again.
bool busy() { return bivouac::runtime::arrivalPending(); }
void enabled(bool on) {
    if (on) bivouac::runtime::switchOn();
    else bivouac::runtime::switchOff();
    Logging.Log("[bivouac] SWITCHED %s", on ? "on" : "off");
}
#endif

}  // namespace bivouac::suite

#ifndef TOTK_SUITE_MEMBER
extern "C" void exl_main(void* /*x0*/, void* /*x1*/) {
    // Identifies the game before any game place is resolved. bivouac::runtime::install()
    // initialises the hook library itself, and this needs only the module layout.
    if (!totk::port::start("bivouac")) return;
    (void)bivouac::suite::install(0);
}

// Referenced by exlaunch's crt0. This module never runs as a process.
extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("unreachable");
}
#endif
