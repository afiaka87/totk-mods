// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "SortPanel.hpp"
#include "GameVersion.hpp"
#include "ColumnPanel.hpp"
#include "Runtime.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <cstring>
#include <lib.hpp>

namespace material_columns::native {
namespace {
std::uintptr_t base{};
bool installed{};
template<class T> T field(std::uintptr_t object, std::size_t offset) {
    return *reinterpret_cast<const T*>(object + offset);
}
#ifdef MATERIAL_COLUMNS_HOST_TEST
std::uintptr_t resolveTestFunction(std::uintptr_t offset);
template<class F> F function(std::uintptr_t offset) { return reinterpret_cast<F>(resolveTestFunction(offset)); }
#else
template<class F> F function(std::uintptr_t offset) { return reinterpret_cast<F>(base + nativeOffset(offset)); }
#endif
std::uintptr_t materialOwner(std::uintptr_t parts) {
    const auto owner = parts ? field<std::uintptr_t>(parts, 24) : 0;
    return owner && field<unsigned>(owner, 440) == 3 &&
        field<std::uintptr_t>(owner, 432) == parts && columnPanelReady(owner) &&
        field<int>(parts, 72) == 5 ? owner : 0;
}
unsigned index(std::uintptr_t owner) {
    if (materialColumnsActive(owner)) return 4;
    const auto child = field<std::uintptr_t>(owner, 424);
    const auto pattern = function<unsigned (*)(std::uintptr_t, int)>(0x1acf8ac)(child, 3);
    return pattern >= 6 && pattern <= 9 ? pattern - 6 : 0;
}
// MessageString::setMessage1235FC4 writes precisely this 24-byte representation.
// LayoutEx copies the text into the native TextBox; this static label also remains valid.
struct Message { const char16_t* text; std::uint32_t length, padding; std::uint64_t metadata; };
static_assert(sizeof(Message) == 24);
constexpr Message label{u"Material Columns", 16, 0, ~std::uint64_t{0}};

HOOK_DEFINE_TRAMPOLINE(SetSortCount) {
    static std::uintptr_t Callback(std::uintptr_t parts, int count) {
        const bool material = materialOwner(parts) != 0;
        const auto result = Orig(parts, material ? 5 : count);
        const auto layout = field<std::uintptr_t>(parts, 32);
        for (unsigned i = 0; i < 5; ++i) {
            char name[] = "Pa_Page_00";
            name[9] = static_cast<char>('0' + i);
            const auto pane = function<std::uintptr_t (*)(std::uintptr_t, const char*)>(0x7012ec)(layout, name);
            if (!pane) continue;
            // Pane::Initialize4620C copies native translation to48, visibility to88.
            *reinterpret_cast<float*>(pane + 48) = static_cast<float>(i) * 18.f - (material ? 36.f : 27.f);
            auto& flags = *reinterpret_cast<std::uint8_t*>(pane + 88);
            flags |= 0x10;
            if (i == 4) flags = static_cast<std::uint8_t>((flags & ~1u) | (material ? 1u : 0u));
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(SetSortSelection) {
    static std::uintptr_t Callback(std::uintptr_t parts, unsigned selected, bool immediate) {
        const auto owner = materialOwner(parts);
        return Orig(parts, owner ? index(owner) : selected, immediate);
    }
};
HOOK_DEFINE_TRAMPOLINE(SetSortMessage) {
    static std::uintptr_t Callback(std::uintptr_t layout, const char* pane,
                                   const void* message, bool resize, std::uintptr_t options) {
        // The native change animation rewrites both text panes after setSortTypeText.
        // Intercept these exact destinations so every phase uses the fifth-mode label.
        if (layout && pane && (!std::strcmp(pane, "T_Sort_00") || !std::strcmp(pane, "T_Sort_01"))) {
            const auto owner = field<std::uintptr_t>(layout, 128);
            if (owner && materialColumnsActive(owner)) {
                const auto parts = field<std::uintptr_t>(owner, 432);
                if (materialOwner(parts) && field<std::uintptr_t>(parts, 32) == layout) message = &label;
            }
        }
        return Orig(layout, pane, message, resize, options);
    }
};
} // namespace

bool installSortPanel(std::uintptr_t mainBase, bool validateOnly) {
    if (installed) return true;
    if (!mainBase) { Logging.Log("[material-columns] sort panel refused: base=0"); return false; }
    const totk::engine::CodeSite sites[]{
        nativeSite(totk::engine::CodeSite{0x1ad000c, 0xa9bd7bfd, 0xa90157f6}), nativeSite(totk::engine::CodeSite{0x1ad0084, 0xa9bc7bfd, 0xf9000bf7}),
        nativeSite(totk::engine::CodeSite{0xb519b0, 0xa9bd7bfd, 0xf9000bf5})
    };
    if (!totk::engine::codeMatches(mainBase, sites, "material-columns", "SORT_PANEL_REFUSED")) return false;
    if (validateOnly) return true;
    base = mainBase;
    SetSortCount::InstallAtOffset(sites[0].offset);
    SetSortSelection::InstallAtOffset(sites[1].offset);
    SetSortMessage::InstallAtOffset(sites[2].offset);
    installed = true;
    return true;
}
void refreshSortPanel(std::uintptr_t screen) {
    if (!installed || !screen) return;
    const auto parts = field<std::uintptr_t>(screen, 432);
    if (!materialOwner(parts)) return;
    function<void (*)(std::uintptr_t, int)>(0x1ad000c)(parts, 5);
    // Preserve native selected/unselected animations for all five dots.
    function<void (*)(std::uintptr_t, unsigned, bool)>(0x1ad0084)(parts, index(screen), true);
    if (materialColumnsActive(screen)) {
        const auto layout = field<std::uintptr_t>(parts, 32);
        using SetMessage = void (*)(std::uintptr_t, const char*, const void*, bool, std::uintptr_t);
        for (const auto* pane : {"T_Sort_00", "T_Sort_01"})
            function<SetMessage>(0xb519b0)(layout, pane, &label, true, 0);
    }
}
} // namespace material_columns::native
