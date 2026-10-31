// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <lib.hpp>
#include "native/LayoutResources.hpp"
#include "native/NativeInventory.hpp"
#include "native/ColumnPanel.hpp"
#include "native/Runtime.hpp"
#include "native/SortPanel.hpp"
#include "native/MemoryProfile.hpp"
#include "native/GameVersion.hpp"
#include "totk/engine/CodeCheck.hpp"

extern "C" void exl_main(void*, void*) {
    using namespace material_columns::native;
    const auto& main = exl::util::GetMainModuleInfo();
    const auto base = exl::util::modules::GetTargetStart();
    if (!selectNativeVersion(base, main.m_Text.m_Size, main.m_Total.m_Size)) {
        Logging.Log("[material-columns] unsupported or ambiguous game build; nothing installed");
        return;
    }
    if (is142()) for (const auto& entry : native142Entries) {
        const totk::engine::CodeSite site{entry.target, entry.words[0], entry.words[1]};
        if (!totk::engine::codeMatches(base, {&site, 1}, "material-columns", "NATIVE_PROFILE_REFUSED")) return;
    }
    // All code fingerprints are validated before the first layout or code hook changes.
    if (!installLayoutResources(base, true) || !installInventoryProbe(base, true) ||
        !installColumnPanel(base, true) || !installRuntime(base, true) || !installSortPanel(base, true) || !memory::install(base, true)) {
        Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " preflight failed; nothing installed");
        return;
    }
    Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " preflight passed; installing hooks");
    exl::hook::Initialize();
    if (!installInventoryProbe(base) || !installRuntime(base) || !installColumnPanel(base) ||
        !installSortPanel(base) || !installLayoutResources(base) || !memory::install(base)) {
        Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " installation failed after preflight");
        return;
    }
    Logging.Log("[material-columns] " MATERIAL_COLUMNS_VERSION " installed for TotK %s", is142() ? "1.4.2" : "1.2.1");
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
