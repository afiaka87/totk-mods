// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeInventory.hpp"
#include "SlotObservation.hpp"
#include "BindingRequest.hpp"
#include "ColumnPanel.hpp"
#include <atomic>
#include <lib.hpp>
#include "totk/engine/CodeCheck.hpp"

namespace material_columns::native {
namespace {
std::uintptr_t base = 0;
bool installed = false;
struct Probe { SlotObservation observation{}; };
std::atomic<Probe*> activeProbe{nullptr};
unsigned repeatedTypeReports = 0;

template<class T> T field(std::uintptr_t object, std::size_t offset) {
    return *reinterpret_cast<const T*>(object + offset);
}
template<class Function> Function function(std::uintptr_t offset) {
    return reinterpret_cast<Function>(base + offset);
}

HOOK_DEFINE_TRAMPOLINE(MaterialSlotCapture) {
    static std::uintptr_t Callback(std::uintptr_t unused, std::uintptr_t slot, const void* parameters) {
        auto* probe = activeProbe.load(std::memory_order_acquire);
        // A unique stack token is passed only through the verified material binding path.
        // Other slots, including calls from other threads, always reach the stock setter.
        if (probe && slot == reinterpret_cast<std::uintptr_t>(probe)) {
            probe->observation = observeSlot(parameters);
            return 0;
        }
        if (retainPooledSlot(slot, parameters)) return 0;
        return Orig(unused, slot, parameters);
    }
};

bool inspect(std::uintptr_t page, std::uint32_t nativeIndex, SlotObservation& result) {
    Probe probe;
    Probe* expected = nullptr;
    if (!activeProbe.compare_exchange_strong(expected, &probe, std::memory_order_acq_rel)) {
        Logging.Log("[material-columns] inventory probe busy at native index %u", nativeIndex);
        return false;
    }
    using Binder = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uint32_t, std::uintptr_t);
    const BindingRequest request{static_cast<std::int32_t>(nativeIndex + 1), 0,
                                 field<std::uintptr_t>(page, 8), page, 0};
    function<Binder>(0x1a7f090)(0, reinterpret_cast<std::uintptr_t>(&probe), nativeIndex + 1,
                             reinterpret_cast<std::uintptr_t>(&request));
    activeProbe.store(nullptr, std::memory_order_release);
    result = probe.observation;
    if (!result.captured)
        Logging.Log("[material-columns] native binding produced no slot record for index %u", nativeIndex);
    return result.captured;
}
} // namespace

bool installInventoryProbe(std::uintptr_t mainBase, bool validateOnly) {
    if (installed) return true;
    if (!mainBase) {
        Logging.Log("[material-columns] inventory probe refused: main base=0");
        return false;
    }
    constexpr totk::engine::CodeSite sites[] = {
        {0x1a58ee0, 0xd107c3ff, 0xa91b7bfd},
        {0x1a7f090, 0xd102c3ff, 0xa9057bfd}
    };
    // BotW Abilities and Arrowbound also intercept setSlot; preserve their trampoline chain.
    if (!totk::engine::codeMatches(mainBase, sites, "material-columns", "INVENTORY_HOOK_REFUSED"))
        return false;
    if (validateOnly) return true;
    base = mainBase;
    MaterialSlotCapture::InstallAtOffset(sites[0].offset);
    installed = true;
    return true;
}

bool readInventory(std::uintptr_t screen, InventoryEntry* entries,
                   std::size_t capacity, std::size_t& count) {
    count = 0;
    if (!installed || !screen || !entries || field<std::uint32_t>(screen, 440) != 3) {
        Logging.Log("[material-columns] inventory read refused: installed=%u screen=%llx buffer=%llx",
                    static_cast<unsigned>(installed), static_cast<unsigned long long>(screen),
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(entries)));
        return false;
    }
    const auto stock = field<std::uintptr_t>(screen, 424);
    const auto controller = stock ? field<std::uintptr_t>(stock, 80) : 0;
    const auto pages = controller ? field<std::uintptr_t>(controller, 96) : 0;
    const auto page = pages && field<std::int32_t>(controller, 88) > 0 ? field<std::uintptr_t>(pages, 0) : 0;
    const auto logicalCount = controller ? field<std::int32_t>(controller, 136) : 0;
    // The stock material list has one native blank/action slot before inventory records.
    if (!page || logicalCount < 1 || static_cast<std::size_t>(logicalCount - 1) > capacity ||
        field<std::uint8_t>(stock, 154) != 1) {
        Logging.Log("[material-columns] invalid stock list: page=%llx count=%d capacity=%u",
                    static_cast<unsigned long long>(page), logicalCount, static_cast<unsigned>(capacity));
        return false;
    }
    const auto pouchSlot = field<std::uintptr_t>(base, 0x4630020);
    const auto pouch = pouchSlot ? field<std::uintptr_t>(pouchSlot, 0) : 0;
    if (!pouch) {
        Logging.Log("[material-columns] inventory read refused: pouch manager missing");
        return false;
    }
    using ReservedDrop = std::uint32_t (*)(std::uintptr_t, std::uint32_t, std::uint32_t, std::uint32_t*);
    for (std::int32_t index = 0; index < logicalCount - 1; ++index) {
        SlotObservation observation;
        if (!inspect(page, static_cast<std::uint32_t>(index), observation)) return false;
        std::uint32_t category = 5;
        const auto physical = function<ReservedDrop>(0x19aefb0)(pouch, static_cast<std::uint32_t>(index), 5, &category);
        if (category != 5 && category != 7) {
            Logging.Log("[material-columns] unexpected shortcut category=%u index=%d", category, index);
            return false;
        }
        entries[index] = {observation.item, static_cast<std::uint32_t>(index), category == 7,
                          observation.eligible};
        if (observation.eligible && repeatedTypeReports < 8) {
            for (std::int32_t prior = 0; prior < index; ++prior) {
                if (!entries[prior].eligible || entries[prior].id != observation.item) continue;
                std::uint32_t priorCategory = 5;
                const auto priorPhysical = function<ReservedDrop>(0x19aefb0)(pouch, prior, 5, &priorCategory);
                Logging.Log("[material-columns] repeated type retained actor=%llx shortcuts=%d/%d categories=%u/%u pouch=%u/%u quantity=%d",
                    static_cast<unsigned long long>(observation.item), prior, index,
                    priorCategory, category, priorPhysical, physical, observation.quantity);
                ++repeatedTypeReports;
                break;
            }
        }
    }
    count = static_cast<std::size_t>(logicalCount - 1);
    return true;
}
bool readInventoryItem(std::uintptr_t screen, std::uint32_t nativeIndex, SlotObservation& result) {
    if (!installed || !screen || field<std::uint32_t>(screen, 440) != 3) return false;
    const auto stock = field<std::uintptr_t>(screen, 424);
    const auto controller = stock ? field<std::uintptr_t>(stock, 80) : 0;
    const auto pages = controller ? field<std::uintptr_t>(controller, 96) : 0;
    const auto page = pages && field<std::int32_t>(controller, 88) > 0 ? field<std::uintptr_t>(pages, 0) : 0;
    const auto count = controller ? field<std::int32_t>(controller, 136) : 0;
    if (!page || count < 1 || nativeIndex >= static_cast<std::uint32_t>(count - 1) ||
        field<std::uint8_t>(stock, 154) != 1) return false;
    return inspect(page, nativeIndex, result);
}
} // namespace material_columns::native
