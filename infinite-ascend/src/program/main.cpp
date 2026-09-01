// Standalone exlaunch host for Infinite Ascend.
#include <lib.hpp>

#include "modules/zonai-ascend/HookInstaller.hpp"

extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();
    const std::uintptr_t mainBase = exl::util::modules::GetTargetStart();
    (void)zonai_ascend::hooks::install(mainBase);
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("unreachable");
}
