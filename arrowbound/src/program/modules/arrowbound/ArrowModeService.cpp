// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ArrowModeService.hpp"

#include <atomic>

#include "AimRaycaster.hpp"
#include "ArrowHookshot.hpp"
#include "ArrowSettings.hpp"
#include "CarrierScan.hpp"
#include "CarrierSaveFilter.hpp"
#include "CarrierSlotAppearance.hpp"
#include "EmblemText.hpp"
#include "PouchSelection.hpp"
#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace arrowbound::arrow_mode {
namespace {
using namespace arrowbound::pure;

constexpr std::uint32_t kContentArrayHash = 0x4290322E;
constexpr std::uint32_t kNameFieldHash = 0x25EFA387;
constexpr std::uint32_t kKeyItemCategory = 8;
constexpr std::ptrdiff_t kKeyItemStructHandle = 0x88;
constexpr int kActionEnable = 15;
constexpr int kActionDisable = 16;
constexpr std::ptrdiff_t kActionWindow = 496;
constexpr int kGrantPushes = 6;
constexpr int kProbeRetryTicks = aim::kTimeoutTicks + 3;
constexpr std::uint64_t kGrantRetryTicks = 300;

struct State {
    std::uintptr_t base = 0;
    const profiles::Inventory* inventory = profiles::inventory(profiles::GameVersion::V121);
    const profiles::Menu* menu = profiles::menu(profiles::GameVersion::V121);
    std::atomic<std::uint32_t> selectedCarrier{0};
    std::atomic<bool> selectingCarrier{false};
    std::atomic<bool> codeOnlyReady{false};
    std::atomic<int> lastAppearance{-1};
    std::atomic<bool> carrierRendered{false};
    std::uint32_t worldGeneration = 0;
    std::uint32_t probeSequence = 0;
    std::uint64_t probeTick = 0;
    std::uint64_t lastScanTick = 0;
    std::uint64_t lastGrantAttemptTick = 0;
    CarrierPresence carrierPresence = CarrierPresence::Unknown;
};

State g{};

bool okPtr(std::uintptr_t value) {
    return totk::engine::isPlausibleAddress(value);
}

bool nameEq(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

std::uintptr_t resolveGmd() {
    if (!g.base) return 0;
    const auto& profile = *g.inventory;
    auto manager = *reinterpret_cast<const std::uintptr_t*>(
        g.base + profile.gameDataManagerSlot);
    if (!okPtr(manager)) return 0;
    if (profile.gameDataDereferences == 2)
        manager = *reinterpret_cast<const std::uintptr_t*>(manager);
    return okPtr(manager) ? manager : 0;
}

std::uintptr_t resolvePouchMgr() {
    if (!g.base) return 0;
    const auto slot = *reinterpret_cast<const std::uintptr_t*>(
        g.base + g.inventory->pouchManagerSlot);
    if (!okPtr(slot)) return 0;
    const auto manager = *reinterpret_cast<const std::uintptr_t*>(slot);
    return okPtr(manager) ? manager : 0;
}

bool queueHasRoom(std::uintptr_t store, int pushes) {
    using namespace totk::engine::layout;
    if (!okPtr(store)) return false;
    const auto capacity = *reinterpret_cast<const std::int32_t*>(
        store + kGameDataQueueCapacity);
    const auto buffer = *reinterpret_cast<const std::uintptr_t*>(
        store + kGameDataQueueBuffer);
    const auto writeIndex = static_cast<std::int32_t>(
        *reinterpret_cast<const std::uint32_t*>(
            store + kGameDataQueueControl) &
        kGameDataQueueIndexMask);
    return capacity > 0 && okPtr(buffer) &&
           writeIndex + pushes + kGameDataQueueHeadroom <= capacity;
}

bool intStoresHaveRoom(std::uintptr_t manager, int pushes) {
    return queueHasRoom(manager + 200, pushes) &&
           queueHasRoom(manager + 296, pushes);
}

const char* slotName(std::uint32_t category, std::uint32_t index,
                     unsigned char record[16]) {
    if (category != kKeyItemCategory) return nullptr;
    const auto manager = resolveGmd();
    const auto pouchMgr = resolvePouchMgr();
    if (!manager || !pouchMgr) return nullptr;
    using GetByIndexFn = std::uint32_t (*)(std::uintptr_t, void*,
                                           std::uintptr_t, std::uint32_t,
                                           std::uint32_t);
    using GetStringFn = std::uint32_t (*)(std::uintptr_t, const char**,
                                          const void*, std::uint32_t);
    const auto getByIndex = reinterpret_cast<GetByIndexFn>(
        g.base + g.inventory->structByIndex);
    const auto getString = reinterpret_cast<GetStringFn>(
        g.base + g.inventory->structString64);
    const auto handle = pouchMgr + kKeyItemStructHandle;
    if (!okPtr(handle)) return nullptr;
    for (int i = 0; i < 16; ++i) record[i] = 0;
    if ((getByIndex(manager, record, handle, kContentArrayHash, index) & 1u) ==
        0) {
        return nullptr;
    }
    const char* name = *reinterpret_cast<const char* const*>(
        g.base + g.inventory->initialEmptyStringSlot);
    if ((getString(manager, &name, record, kNameFieldHash) & 1u) == 0)
        return nullptr;
    return name;
}

CarrierPresence findCarrier() {
    const auto pouchMgr = resolvePouchMgr();
    if (!pouchMgr || !resolveGmd()) return CarrierPresence::Unknown;
    using GetCountFn = std::uint32_t (*)(std::uintptr_t, std::uint32_t);
    const auto count = reinterpret_cast<GetCountFn>(g.base + g.inventory->pouchCount)(
        pouchMgr, kKeyItemCategory);
    alignas(8) unsigned char record[16]{};
    return scanCarrier(count,
        [&](std::uint32_t i) { return slotName(kKeyItemCategory, i, record); },
        [](const char* name) { return nameEq(name, kCarrierActor); });
}

bool grantCarrier() {
    const auto manager = resolveGmd();
    const auto pouchMgr = resolvePouchMgr();
    if (!manager || !pouchMgr || !intStoresHaveRoom(manager, kGrantPushes)) {
        ZHLOG("ARROW_MODE_GRANT_DEFER manager=%p pouch=%p queue_room=0",
              reinterpret_cast<void*>(manager), reinterpret_cast<void*>(pouchMgr));
        return false;
    }

    struct NameRef {
        const char* cstr;
        std::uint64_t pad[3];
    } nameRef{kCarrierActor, {0, 0, 0}};
    auto* name = reinterpret_cast<const char**>(&nameRef);
    using ResolveCategoryFn = std::uint8_t (*)(std::uint32_t*, const char**);
    const auto resolveCategory = reinterpret_cast<ResolveCategoryFn>(
        g.base + g.inventory->resolveCategory);
    std::uint32_t category = 0;
    if ((resolveCategory(&category, name) & 1u) == 0 ||
        category != kKeyItemCategory) {
        ZHLOG("ARROW_MODE_GRANT category resolve failed cat=%u", category);
        return false;
    }

    using AddToPouchFn = std::uint64_t (*)(
        void*, const char**, void*, std::uint32_t, std::int32_t, std::uint8_t,
        std::int32_t, std::uint8_t, std::uint32_t, std::uint32_t,
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint32_t*, std::uint8_t);
    const auto addToPouch = reinterpret_cast<AddToPouchFn>(g.base + g.inventory->addToPouch);
    void* modifier = *reinterpret_cast<void**>(g.base + g.inventory->cEmptyStringSlot);
    const std::uint64_t result = addToPouch(
        reinterpret_cast<void*>(pouchMgr), name, modifier, category, 1, 1, 0,
        0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, nullptr, 1);
    ZHLOG("ARROW_MODE_GRANT actor=%s cat=%u result=%p", kCarrierActor,
          category, reinterpret_cast<void*>(result));
    return result != 0;
}

void requestGameplayProbe(HookshotRuntime& rt) {
    if (g.probeSequence || !world::havePlayer()) return;
    const Vec3 from = world::playerPosition();
    const Vec3 direction{0.0f, 1.0f, 0.0f};
    const Vec3 to = add(from, mul(direction, 0.25f));
    const std::uint32_t before = rt.aim.requestSeq;
    if (aim::request(from, to, from, direction, rt.aim.requestSeq,
                     rt.session.worldGen, rt.session.tick)) {
        g.probeSequence = rt.aim.requestSeq;
        g.probeTick = rt.session.tick;
        ZHLOG("ARROW_MODE_PROBE seq=%u", g.probeSequence);
    } else {
        rt.aim.requestSeq = before;
    }
}

void serviceLiveWork(HookshotRuntime& rt) {
    const bool needsGrant = carrierGrantDue(g.carrierPresence, rt.session.tick,
                                            g.lastGrantAttemptTick, kGrantRetryTicks);
    if (!needsGrant || !g.codeOnlyReady.load(std::memory_order_acquire)) return;
    if (!world::ready() || rt.arrowTrip.phase != ArrowPhase::Idle) {
        return;
    }

    if (sampleArrived(rt.aim.sample, rt.aim.sampleTick, g.probeSequence, rt.session.tick)) {
        g.probeSequence = 0;
        rt.aim.starved = 0;
        g.lastGrantAttemptTick = rt.session.tick;
        g.carrierPresence = findCarrier();
        if (g.carrierPresence == CarrierPresence::Present) {
            ZHLOG("ARROW_MODE_CARRIER_PRESENT live_scan=1");
        } else if (g.carrierPresence == CarrierPresence::Unknown) {
            ZHLOG("ARROW_MODE_GRANT_DEFER incomplete pouch scan");
        } else if (grantCarrier()) {
            g.carrierPresence = CarrierPresence::Present;
            ZHLOG("ARROW_MODE_CARRIER_GRANTED");
        }
        return;
    }
    if (g.probeSequence &&
        rt.session.tick - g.probeTick >
            static_cast<std::uint64_t>(kProbeRetryTicks)) {
        g.probeSequence = 0;
    }
    if (!g.probeSequence) requestGameplayProbe(rt);
}

}  // namespace

void initialize(std::uintptr_t mainBase) { g.base = mainBase; }

bool useGameProfile(profiles::GameVersion version) {
    const auto* inventory = profiles::inventory(version);
    const auto* menu = profiles::menu(version);
    if (!inventory || !menu) return false;
    g.inventory = inventory;
    g.menu = menu;
    return true;
}

void filterSerializedSave(void* rawManager, std::uint32_t fileIndex) {
    if (fileIndex != 0) return; // progress.sav only
    const auto manager = reinterpret_cast<std::uintptr_t>(rawManager);
    if (!okPtr(manager)) {
        ZHLOG("ARROW_SAVE_FILTER refused manager=%p", rawManager);
        return;
    }
    // serializeSaveData writes +0x60 buffer / +0x58 size, then its caller sets ready +0x79.
    const auto count = *reinterpret_cast<const std::uint32_t*>(manager + 0xCC0);
    const auto files = *reinterpret_cast<const std::uintptr_t*>(manager + 0xCC8);
    if (count == 0 || count > 16 || !okPtr(files)) {
        ZHLOG("ARROW_SAVE_FILTER refused files=%p count=%u", reinterpret_cast<void*>(files), count);
        return;
    }
    const auto buffer = *reinterpret_cast<const std::uintptr_t*>(files + 0x60);
    const auto size = *reinterpret_cast<const std::uint32_t*>(files + 0x58);
    if (!okPtr(buffer) || size > 16 * 1024 * 1024) {
        ZHLOG("ARROW_SAVE_FILTER refused buffer=%p size=%u", reinterpret_cast<void*>(buffer), size);
        return;
    }
    const auto result = filterCarrierSave(reinterpret_cast<unsigned char*>(buffer), size);
    ZHLOG("ARROW_SAVE_FILTER result=%u size=%u (0=invalid 1=unknown 2=earned 3=absent 4=removed)",
          static_cast<unsigned>(result), size);
}

void service(HookshotRuntime& rt) {
    if (g.worldGeneration != rt.session.worldGen) onWorldReset(rt.session.worldGen);

    if (world::ready() && rt.arrowTrip.phase == ArrowPhase::Idle) settings::service();

    if (g.carrierRendered.load(std::memory_order_acquire)) g.carrierPresence = CarrierPresence::Present;
    if (g.carrierPresence != CarrierPresence::Present &&
        (g.lastScanTick == 0 || rt.session.tick - g.lastScanTick >= 120)) {
        const auto presence = findCarrier();
        if (g.lastScanTick == 0 || presence != g.carrierPresence)
            ZHLOG("ARROW_MODE_CARRIER_SCAN presence=%u (0=loading 1=absent 2=present)",
                  static_cast<unsigned>(presence));
        g.lastScanTick = rt.session.tick;
        g.carrierPresence = presence;
    }
    serviceLiveWork(rt);
}

void onWorldReset(std::uint32_t worldGeneration) {
    g.worldGeneration = worldGeneration;
    g.probeSequence = 0;
    g.probeTick = 0;
    g.lastScanTick = 0;
    g.lastGrantAttemptTick = 0;
    g.carrierPresence = CarrierPresence::Unknown;
    g.carrierRendered.store(false, std::memory_order_release);
    g.selectedCarrier.store(0, std::memory_order_release);
    g.selectingCarrier.store(false, std::memory_order_release);
}

bool enabled() {
    return g.codeOnlyReady.load(std::memory_order_acquire) && settings::enabled();
}

void setCodeOnlyReady(bool ready) { g.codeOnlyReady.store(ready, std::memory_order_release); }

std::uintptr_t selectionDeal(std::uintptr_t original) {
    static const char* const deal = "SageSoul_Fire";
    if (!g.selectingCarrier.load(std::memory_order_acquire)) return original;
    return reinterpret_cast<std::uintptr_t>(&deal);
}

bool replaceMessage(void* output, const char* const* table, const char* const* key) {
    if (!output || !table || !key) return false;
    const auto* message = emblemMessage(*table, *key);
    if (!message) return false;
    std::memcpy(output, message, sizeof(*message));
    return true;
}

void beginSelection(std::uint32_t category, std::uint32_t index) {
    bool carrier = false;
    if (category == kKeyItemCategory && g.codeOnlyReady.load(std::memory_order_acquire)) {
        alignas(8) unsigned char record[16]{};
        carrier = nameEq(slotName(category, index, record), kCarrierActor);
    }
    g.selectedCarrier.store(carrier ? 1u : 0u, std::memory_order_release);
    g.selectingCarrier.store(carrier, std::memory_order_release);
    if (category == kKeyItemCategory)
        ZHLOG("ARROW_MODE_SELECT index=%u carrier=%u", index, (unsigned)carrier);
}

void endSelection(void* rawScreen) {
    g.selectingCarrier.store(false, std::memory_order_release);
    if (!g.selectedCarrier.load(std::memory_order_acquire)) return;
    const auto screen = reinterpret_cast<std::uintptr_t>(rawScreen);
    if (!okPtr(screen)) return;
    const auto window = *reinterpret_cast<const std::uintptr_t*>(screen + kActionWindow);
    if (!okPtr(window)) {
        ZHLOG("ARROW_MODE_MENU missing action window");
        return;
    }
    using SetText = void (*)(std::uintptr_t, unsigned, int, const void*);
    using SetEnabled = void (*)(std::uintptr_t, unsigned, bool);
    const bool active = enabled();
    const auto& message = active ? kDeactivate : kActivate;
    reinterpret_cast<SetText>(g.base + g.menu->setButtonText)(
        window, 0, active ? kActionDisable : kActionEnable, &message);
    reinterpret_cast<SetEnabled>(g.base + g.menu->setButtonEnabled)(window, 0, true);
    ZHLOG("ARROW_MODE_MENU enabled=%u code_only=1", (unsigned)active);
}

bool beginAction(int action) {
    if (g.selectedCarrier.exchange(0, std::memory_order_acq_rel) == 0 ||
        (action != kActionEnable && action != kActionDisable)) {
        return false;
    }
    const bool desired = action == kActionEnable;
    settings::request(desired);
    ZHLOG("ARROW_MODE_UI enabled=%u", (unsigned)desired);
    return true;
}

bool isCarrierSlot(const void* options) {
    if (!okPtr(reinterpret_cast<std::uintptr_t>(options))) return false;
    const auto name = *static_cast<const char* const*>(options);
    const auto address = reinterpret_cast<std::uintptr_t>(name);
    const bool carrier = address >= 0x1000 && address < (1ull << 40) && nameEq(name, kCarrierActor);
    if (carrier) {
        g.carrierRendered.store(true, std::memory_order_release);
        const int active = enabled() ? 1 : 0;
        if (g.lastAppearance.exchange(active, std::memory_order_relaxed) != active)
            ZHLOG("ARROW_MODE_ICON enabled=%d carrier_match=1", active);
    }
    return carrier;
}

void refreshSelectedSlot(void* rawScreen) {
    const auto screen = reinterpret_cast<std::uintptr_t>(rawScreen);
    const auto pouch = resolvePouchMgr();
    if (!okPtr(screen) || !pouch) {
        ZHLOG("ARROW_MODE_REFRESH unavailable screen=%p pouch=%p", rawScreen,
              reinterpret_cast<void*>(pouch));
        return;
    }
    std::uint32_t category = 0, index = 0;
    using GetActive = void (*)(std::uintptr_t, std::uint32_t*, std::uint32_t*);
    reinterpret_cast<GetActive>(g.base + g.menu->getActive)(pouch, &category, &index);
    alignas(8) unsigned char record[16]{};
    if (category != kKeyItemCategory || !nameEq(slotName(category, index, record), kCarrierActor)) {
        ZHLOG("ARROW_MODE_REFRESH selection changed category=%u index=%u", category, index);
        return;
    }
    const auto pointer = [](std::uintptr_t p) { return *reinterpret_cast<const std::uintptr_t*>(p); };
    const auto integer = [](std::uintptr_t p) { return *reinterpret_cast<const std::uint32_t*>(p); };
    const auto pane = pointer(screen + 0x1E0);
    using GetControl = std::uintptr_t (*)(std::uintptr_t);
    const auto control = okPtr(pane) ? reinterpret_cast<GetControl>(g.base + g.menu->getControl)(pane) : 0;
    const auto slot = engine::selectedKeyItemSlot(control, index, okPtr, pointer, integer);
    if (!slot) {
        ZHLOG("ARROW_MODE_REFRESH no live selected slot index=%u", index);
        return;
    }
    using SetCheck = void (*)(std::uintptr_t, bool);
    using GetIcon = std::uintptr_t (*)(std::uintptr_t);
    using SetIcon = void (*)(std::uintptr_t, const char**, bool, bool);
    using SetState = void (*)(std::uintptr_t, std::uint32_t);
    const bool active = enabled();
    reinterpret_cast<SetCheck>(g.base + g.menu->setCheck)(slot, active);
    const auto icon = reinterpret_cast<GetIcon>(g.base + g.menu->getIcon)(slot);
    if (okPtr(icon)) {
        const char* iconActor = engine::kActiveCarrierIconActor;
        if (g.menu->setIconHasActiveArg) {
            reinterpret_cast<SetIcon>(g.base + g.menu->setIcon)(icon, &iconActor, false, active);
        } else {
            using SetIconWithoutActive = void (*)(std::uintptr_t, const char**, bool);
            reinterpret_cast<SetIconWithoutActive>(g.base + g.menu->setIcon)(icon, &iconActor, false);
        }
        reinterpret_cast<SetState>(g.base + g.menu->setState)(icon, engine::carrierIconState(active));
    }
    ZHLOG("ARROW_MODE_REFRESH enabled=%u state=%u icon=%u", (unsigned)active,
          engine::carrierIconState(active), (unsigned)okPtr(icon));
}

}  // namespace arrowbound::arrow_mode
