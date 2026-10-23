// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "KeyItems.hpp"
#include "KeyItemUI.hpp"
#include "GameData.hpp"
#include "ChampionSaveSchema.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/GameData.hpp"
#include "totk/engine/KeyItemGrant.hpp"
#include "totk/engine/PouchSelection.hpp"
#include <lib.hpp>

namespace botw_abilities::champions::items {
namespace {
using namespace totk::engine;
constexpr CodeSite kSites[]{
    {0x1B16650, 0xD10343FF, 0}, {0x1B1241C, 0xFC190FEA, 0},
    {0x1A58EE0, 0xD107C3FF, 0}, {0x1B171AC, 0x910083E0, 0},
};
std::uintptr_t g_base{};
ItemSelection g_selected;
bool g_selecting{};
std::uint64_t g_generation = UINT64_MAX, g_nextScan{};
bool g_granted{};
EmptyPouch g_empty;

template <class Fn> Fn call(std::uintptr_t offset) { return reinterpret_cast<Fn>(g_base + offset); }
std::uintptr_t manager(std::uintptr_t slot) {
    const auto result = resolveGameDataManagerFromHolder(g_base + slot);
    return result ? result.value : 0;
}

const char* slotName(std::uint32_t category, std::uint32_t index) {
    if (category != kKeyItemCategory || index >= 200) return nullptr;
    const auto gmd = manager(0x462E3D8), pouch = manager(0x4630020);
    if (!gmd || !pouch) return nullptr;
    alignas(8) unsigned char record[16]{};
    using ByIndex = std::uint32_t(*)(std::uintptr_t, void*, std::uintptr_t, std::uint32_t, std::uint32_t);
    using Name = std::uint32_t(*)(std::uintptr_t, const char**, const void*, std::uint32_t);
    if (!(call<ByIndex>(0x942FE8)(gmd, record, pouch + 0x88, 0x4290322E, index) & 1)) return nullptr;
    auto name = readMemory<const char*>(g_base + 0x462E1B8);
    if (!(call<Name>(0x935BE0)(gmd, &name, record, 0x25EFA387) & 1) ||
        !isPlausibleStringAddress(reinterpret_cast<std::uintptr_t>(name)) || !std::memchr(name, 0, 64))
        return nullptr;
    return name;
}

struct Message {
    const char16_t* text;
    std::int32_t length;
    std::uint32_t padding = 0;
    std::uint64_t attributes = UINT64_MAX;
};
static_assert(sizeof(Message) == 24);
constexpr Message kActivate{u"Activate", 8}, kDeactivate{u"Deactivate", 10};

void configureAction(std::uintptr_t screen) {
    const auto snapshot = data::state().snapshot();
    if (!snapshot.ready || snapshot.generation != g_selected.generation || g_selected.index < 0) return;
    const auto window = isPlausibleAddress(screen) ? readMemory<std::uintptr_t>(screen + 496) : 0;
    if (!isPlausibleAddress(window)) {
        Logging.Log("[champions] ITEM_MENU_REFUSED reason=window");
        return;
    }
    const bool active = snapshot.states[g_selected.index].enabled;
    call<void(*)(std::uintptr_t, unsigned, int, const Message*)>(0x1ABD660)(
        window, 0, active ? 16 : 15, active ? &kDeactivate : &kActivate);
    call<void(*)(std::uintptr_t, unsigned, bool)>(0x1B17D78)(window, 0, true);
}

void refresh(std::uintptr_t screen) {
    const auto pouch = manager(0x4630020);
    if (!pouch || !isPlausibleAddress(screen)) return;
    std::uint32_t category{}, index{};
    call<void(*)(std::uintptr_t, std::uint32_t*, std::uint32_t*)>(0x1A7A010)(pouch, &category, &index);
    const auto item = keyItemIndex(kSaveKeys, slotName(category, index));
    const auto snapshot = data::state().snapshot();
    if (item < 0 || !snapshot.ready) return;
    const auto pane = readMemory<std::uintptr_t>(screen + 0x1E0);
    const auto control = isPlausibleAddress(pane) ? call<std::uintptr_t(*)(std::uintptr_t)>(0x1AC2528)(pane) : 0;
    const auto slot = selectedKeyItemSlot(control, index, isPlausibleAddress,
        [](std::uintptr_t address) { return readMemory<std::uintptr_t>(address); },
        [](std::uintptr_t address) { return readMemory<std::uint32_t>(address); });
    if (!slot) {
        Logging.Log("[champions] ITEM_REFRESH_REFUSED index=%u reason=slot", index);
        return;
    }
    const bool active = snapshot.states[item].enabled;
    call<void(*)(std::uintptr_t, bool)>(0x1A583A8)(slot, active);
    const auto icon = call<std::uintptr_t(*)(std::uintptr_t)>(0x1A58378)(slot);
    if (isPlausibleAddress(icon)) {
        const char* name = kSaveKeys[item].actor;
        call<void(*)(std::uintptr_t, const char**, bool, bool)>(0x1A594FC)(icon, &name, false, active);
        call<void(*)(std::uintptr_t, std::uint32_t)>(0x1A58464)(icon, active ? 1 : 4);
    }
}

HOOK_DEFINE_TRAMPOLINE(Select) {
    static std::uint64_t Callback(std::uintptr_t screen, std::uint32_t category,
                                  std::uint32_t index, void* argument) {
        const auto snapshot = data::state().snapshot();
        g_selected = {snapshot.ready ? keyItemIndex(kSaveKeys, slotName(category, index)) : -1,
                      snapshot.generation};
        g_selecting = g_selected.index >= 0;
        const auto result = Orig(screen, category, index, argument);
        g_selecting = false;
        configureAction(screen);
        return result;
    }
};

HOOK_DEFINE_INLINE(Deal) {
    static void Callback(exl::hook::InlineCtx* context) {
        static const char* const deal = "SageSoul_Fire";
        if (g_selecting) context->X[1] = reinterpret_cast<std::uintptr_t>(&deal);
    }
};

HOOK_DEFINE_TRAMPOLINE(Action) {
    static std::uint64_t Callback(std::uintptr_t screen, int action) {
        const auto selected = g_selected;
        g_selected = {};
        if (!selected.owns(action)) return Orig(screen, action);
        const bool applied = data::state().enable(selected.generation,
            static_cast<Blessing>(selected.index), action == 15);
        Logging.Log("[champions] ITEM_TOGGLE blessing=%d enabled=%u accepted=%u",
                    selected.index, unsigned(action == 15), unsigned(applied));
        const auto result = Orig(screen, -1);
        refresh(screen);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(SetSlot) {
    static std::uint64_t Callback(void* list, void* slot, void* options) {
        if (!isPlausibleAddress(reinterpret_cast<std::uintptr_t>(options))) return Orig(list, slot, options);
        const auto name = readMemory<const char*>(reinterpret_cast<std::uintptr_t>(options));
        const auto item = isPlausibleStringAddress(reinterpret_cast<std::uintptr_t>(name))
            && std::memchr(name, 0, 64) ? keyItemIndex(kSaveKeys, name) : -1;
        const auto snapshot = data::state().snapshot();
        if (item < 0 || !snapshot.ready) return Orig(list, slot, options);
        const ItemAppearance appearance(options, snapshot.states[item].enabled);
        return Orig(list, slot, options);
    }
};
} // namespace

void service(GameDataWriteReadiness readiness, std::uint64_t tick) {
    const auto snapshot = data::state().snapshot();
    if (!g_base || !snapshot.ready || readiness != GameDataWriteReadiness::LiveGameplayAndWorkerAlive) return;
    if (g_generation != snapshot.generation) {
        g_generation = snapshot.generation;
        g_granted = false;
        g_empty = {};
        g_nextScan = tick;
    }
    if (g_granted || tick < g_nextScan) return;
    g_nextScan = tick + 120;
    const auto pouch = manager(0x4630020), gmd = manager(0x462E3D8);
    if (!pouch || !gmd) return;
    const auto count = call<std::uint32_t(*)(std::uintptr_t, std::uint32_t)>(0xD44A08)(pouch, kKeyItemCategory);
    const auto scan = scanKeyItems(kSaveKeys, count,
        [](std::uint32_t index) { return slotName(kKeyItemCategory, index); }, g_empty.confirmed(count));
    if (!scan.complete) {
        Logging.Log("[champions] ITEM_GRANT_DEFER incomplete-count=%u", count);
        return;
    }
    g_granted = scan.found == 15;
    for (std::size_t i = 0; i < kBlessingCount; ++i) {
        if (scan.found & (1u << i)) continue;
        const auto result = grantKeyItem(gmd, pouch, kSaveKeys[i].actor,
            reinterpret_cast<void* const*>(g_base + 0x462E0F0),
            {call<ResolvePouchCategory>(0x1706D80), call<AddToPouch>(0xCE0EEC)}, readiness);
        Logging.Log("[champions] ITEM_GRANT blessing=%u error=%u native=%llu", unsigned(i),
                    unsigned(result.error), result.nativeResult);
        if (result) g_nextScan = tick + 1;
        break;
    }
}

bool install(std::uintptr_t base) {
    if (!codeMatches(base, kSites, "champions", "ITEM_BUILD_REFUSED")) return false;
    g_base = base;
    Select::InstallAtOffset(0x1B16650);
    Action::InstallAtOffset(0x1B1241C);
    SetSlot::InstallAtOffset(0x1A58EE0);
    Deal::InstallAtOffset(0x1B171AC);
    return true;
}
} // namespace botw_abilities::champions::items
