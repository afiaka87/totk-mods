// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <lib.hpp>
#include "native/LayoutResources.hpp"
#include "native/NativeInventory.hpp"
#include "native/ColumnPanel.hpp"
#include "native/Runtime.hpp"
#include "native/SortPanel.hpp"
#include "native/MemoryProfile.hpp"

extern "C" void exl_main(void*, void*) {
    using namespace material_columns::native;
    const auto& main = exl::util::GetMainModuleInfo();
    const auto base = exl::util::modules::GetTargetStart();
    // Check address extents before reading any version-specific instructions/data.
    if (!base || main.m_Text.m_Size < 0x2a68c40 || main.m_Total.m_Size < 0x4728628) {
        Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " unsupported module text=%zx total=%zx",
                    main.m_Text.m_Size, main.m_Total.m_Size);
        return;
    }
    // All code fingerprints are validated before the first layout or code hook changes.
    if (!installLayoutResources(base, true) || !installInventoryProbe(base, true) ||
        !installColumnPanel(base, true) || !installRuntime(base, true) || !installSortPanel(base, true) || !memory::install(base, true)) {
        Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " preflight failed; nothing installed");
        return;
    }
    exl::hook::Initialize();
    if (!installInventoryProbe(base) || !installRuntime(base) || !installColumnPanel(base) ||
        !installSortPanel(base) || !installLayoutResources(base) || !memory::install(base)) {
        Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " installation failed after preflight");
        return;
    }
    Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " installed for TotK 1.2.1; profile storage on SD; gameplay unverified");
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
