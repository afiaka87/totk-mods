#include <lib.hpp>
#include "StasisInstall.hpp"

extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    stasis::install(exl::util::modules::GetTargetStart());
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
