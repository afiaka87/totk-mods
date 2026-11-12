#include <lib.hpp>
#include "totk/port/CodeCheck.hpp"
#include "StasisInstall.hpp"

extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    if (!totk::port::start("stasis")) return;
    stasis::install(exl::util::modules::GetTargetStart());
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
