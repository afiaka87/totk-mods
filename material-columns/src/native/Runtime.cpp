// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Runtime.hpp"
#include "GameVersion.hpp"
#include "Catalog.hpp"
#include "Categories.hpp"
#include "Localization.hpp"
#include "ColumnPanel.hpp"
#include "NativeInventory.hpp"
#include "PreferenceStorage.hpp"
#include "SortPanel.hpp"
#include "../pure/SelectionState.hpp"
#include "../pure/Navigation.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <lib.hpp>

namespace material_columns::native {
namespace {
using State = SelectionState<1024, maximumColumns>;
State state;
Navigation navigation;
std::array<InventoryEntry, 1024> entries{};
std::uintptr_t base{}, owner{};
bool installed{}, active{}, opened{}, failed{}, watchSort{};
bool dirty{}, restoring{};
float idleFrames{};
unsigned restoreTarget{}, restoreVisited{}, restoreAttempts{};
ItemId restoreItem{};
bool entryFromBeginning{};
unsigned observedSort{};
bool entryPending{};
unsigned entryAttempts{};
ItemId entryItem{};
int snapshotCount = -1;
int selectedQuantity = -1;

template<class T> T& field(std::uintptr_t object, std::size_t offset) {
    return *reinterpret_cast<T*>(object + offset);
}
#ifdef MATERIAL_COLUMNS_HOST_TEST
std::uintptr_t resolveTestFunction(std::uintptr_t offset);
template<class F> F function(std::uintptr_t offset) { return reinterpret_cast<F>(resolveTestFunction(offset)); }
#else
template<class F> F function(std::uintptr_t offset) { return reinterpret_cast<F>(base + nativeOffset(offset)); }
#endif

// Observe the native call made by the Y-button branch. Do not replace the sort operation,
// reorder its results, write a fifth enum, or intercept Remote Bomb's sort writer.
HOOK_DEFINE_TRAMPOLINE(ObserveShortcutSort) {
    static std::uintptr_t Callback(std::uintptr_t pouch, std::uintptr_t mode,
                                   int category, unsigned pattern) {
        const bool fromShortcut = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) == base + nativeOffset(0x1acceec);
        const auto result = Orig(pouch, mode, category, pattern);
        if (watchSort && fromShortcut && mode == 1 && category == 4) observedSort = pattern;
        return result;
    }
};

struct InputMask {
    std::uintptr_t input;
    std::uint32_t mask, pressed, held;
    InputMask(std::uintptr_t inputValue, std::uint32_t maskValue)
        : input(inputValue), mask(maskValue), pressed(field<std::uint32_t>(input, 8)),
          held(field<std::uint32_t>(input, 276)) {
        field<std::uint32_t>(input, 8) &= ~mask;
        field<std::uint32_t>(input, 276) &= ~mask;
    }
    ~InputMask() {
        field<std::uint32_t>(input, 8) = (field<std::uint32_t>(input, 8) & ~mask) | (pressed & mask);
        field<std::uint32_t>(input, 276) = (field<std::uint32_t>(input, 276) & ~mask) | (held & mask);
    }
};
constexpr std::uint32_t stockDirections = 0x0fcc0000u;

void flushPreferences() {
    if (dirty && savePreferences(state.memory())) dirty = false;
    idleFrames = 0;
}
unsigned nativePattern(std::uintptr_t screen) {
    return function<unsigned (*)(std::uintptr_t, int)>(0x1acf8ac)(field<std::uintptr_t>(screen, 424), 3);
}
void beginRestore(unsigned desired, unsigned current) {
    restoring = desired != current;
    restoreTarget = desired;
    restoreVisited = current >= 6 && current <= 9 ? 1u << (current - 6) : 0;
    restoreAttempts = 0;
}

bool bind(void*, std::uint32_t logical, std::uint32_t& slot, bool& selected) {
    return state.bind(logical, slot, selected);
}
ColumnAppearance describe(std::size_t group) {
    const auto* catalog = catalogGroup(state.groupId(group));
    const auto view = state.view(group);
    const auto label=locale::label(state.groupId(group), catalog ? catalog->label : "Other materials");
    return {categories::registry.label(state.groupId(group),label), view.overflow_above, view.overflow_below};
}
bool rebuild(std::uintptr_t screen) {
    std::size_t count = 0;
    if (!readInventory(screen, entries.data(), entries.size(), count)) return false;
    if (!state.rebuild(entries.data(), count, categories::lookup, fallback(false), fallback(true))) {
        Logging.Log("[material-columns] snapshot validation rejected count=%u pending=%u attempt=%u",
            static_cast<unsigned>(count), static_cast<unsigned>(entryPending), entryAttempts);
        for (std::size_t i = 0; i < count; ++i) for (std::size_t j = 0; j < i; ++j) {
            if (entries[i].eligible && entries[j].eligible &&
                entries[i].native_slot == entries[j].native_slot) {
                Logging.Log("[material-columns] duplicate snapshot records rows=%u/%u slots=%u/%u actors=%llx/%llx",
                    static_cast<unsigned>(j), static_cast<unsigned>(i), entries[j].native_slot, entries[i].native_slot,
                    static_cast<unsigned long long>(entries[j].id), static_cast<unsigned long long>(entries[i].id));
                return false;
            }
        }
        return false;
    }
    snapshotCount = static_cast<int>(count);
    return true;
}
bool display(std::uintptr_t screen) {
    if (!configureColumnPanel(screen, state.groupCount(), state.focused(), bind, nullptr, describe)) return false;
    const auto* item = state.selected();
    return !item || focusNativeItem(screen, item->native_slot, true);
}
void fail(std::uintptr_t screen, const char* operation) {
    if (!failed) Logging.Log("[material-columns] menu adapter refused %s; restoring stock view owner=%llx",
                             operation, static_cast<unsigned long long>(screen));
    failed = true;
    entryPending = entryFromBeginning = false;
    active = false;
    navigation.reset();
    hideColumnPanel(screen);
}
void scheduleEntry(ItemId keepItem, bool fromBeginning = false) {
    entryFromBeginning = fromBeginning;
    entryPending = true;
    entryAttempts = 0;
    entryItem = keepItem;
}
bool enter(std::uintptr_t screen, ItemId keepItem, bool retrySnapshot = false) {
    locale::loadOnce();
    categories::loadOnce(locale::language());
    if (!rebuild(screen)) {
        if (!retrySnapshot) fail(screen, "inventory snapshot");
        return false;
    }
    if (entryFromBeginning) { while (state.horizontal(-1)) {} }
    else if (keepItem) state.selectIdentity(keepItem);
    if (!display(screen)) { fail(screen, "panel binding/action focus"); return false; }
    active = true;
    entryPending = false;
    Logging.Log("[material-columns] columns entered groups=%u after %u deferred snapshot attempts",
        static_cast<unsigned>(state.groupCount()), entryAttempts);
    dirty |= entryFromBeginning || keepItem != 0 || !state.memory().columns;
    entryFromBeginning = false;
    state.setColumns(true);
    field<std::uint8_t>(field<std::uintptr_t>(screen, 424), 161) = 1;
    refreshSortPanel(screen);
    navigation.reset();
    return true;
}
float frameStep() {
    const auto clock = nativeSingleton(base, 0x462df08);
    return clock ? field<float>(clock, nativeFrameOffset()) : 0;
}
Move movement(std::uintptr_t input, std::uintptr_t controller) {
    const auto start = field<std::int32_t>(input, 264);
    // UIController uses its configured sead cross-bit origin, not nn::hid bit numbers.
    if (start < 0 || start > 28) { navigation.reset(); return {}; }
    const auto held = field<std::uint32_t>(input, 276);
    return navigation.update(field<float>(input, 296), field<float>(input, 300),
        (held & (1u << (start + 2))) != 0, (held & (1u << (start + 3))) != 0,
        (held & (1u << start)) != 0, (held & (1u << (start + 1))) != 0,
        frameStep(), field<std::uint8_t>(controller, 196), field<std::uint8_t>(controller, 197));
}
} // namespace

bool installRuntime(std::uintptr_t mainBase, bool validateOnly) {
    if (installed) return true;
    if (!mainBase) { Logging.Log("[material-columns] runtime refused: base=0"); return false; }
    const totk::engine::CodeSite sites[]{
        nativeSite(totk::engine::CodeSite{0x1a718a4, 0xa9bf7bfd, 0x910003fd}), nativeSite(totk::engine::CodeSite{0x1acf8ac, 0xd10103ff, 0xa9027bfd})
    };
    if (!totk::engine::codeMatches(mainBase, sites, "material-columns", "RUNTIME_HOOK_REFUSED")) return false;
    if (!initializePreferenceStorage(mainBase, validateOnly)) return false;
    if (validateOnly) return true;
    base = mainBase;
    ObserveShortcutSort::InstallAtOffset(sites[0].offset);
    installed = true;
    return true;
}

std::uintptr_t updateMaterialShortcut(std::uintptr_t child, NativeUpdate original) {
    if (!installed || !child) return original(child);
    const auto screen = field<std::uintptr_t>(child, 24);
    if (screen && field<std::uint32_t>(screen, 440) != 3 && owner == screen) {
        if (opened) flushPreferences();
        hideColumnPanel(screen);
        active = opened = restoring = entryPending = entryFromBeginning = false;
        navigation.reset();
    }
    if (!screen || field<std::uint32_t>(screen, 440) != 3 ||
        field<std::uintptr_t>(screen, 424) != child || !columnPanelReady(screen)) return original(child);
    if (owner != screen) {
        if (owner) { flushPreferences(); hideColumnPanel(owner); }
        owner = screen;
        active = opened = failed = restoring = entryPending = entryFromBeginning = false;
        snapshotCount = -1;
        navigation.reset();
    }
    const bool open = (function<unsigned (*)(std::uintptr_t)>(0x10da870)(screen) & 1) != 0;
    if (!open) {
        if (opened) flushPreferences();
        hideColumnPanel(screen);
        active = opened = failed = restoring = entryPending = entryFromBeginning = false;
        navigation.reset();
        return original(child);
    }
    const auto controller = field<std::uintptr_t>(child, 80);
    const auto layout = field<std::uintptr_t>(child, 32);
    const auto fade = field<std::uintptr_t>(child, 48);
    const bool interactive = controller && layout && fade && field<float>(fade, 32) == 0 &&
        field<std::uint8_t>(layout, 145) == 2 && !field<std::uint8_t>(child, 152) &&
        !field<std::uint8_t>(child, 158);
    if (!interactive || failed) { navigation.reset(); return original(child); }
    const auto vtable = field<std::uintptr_t>(screen, 0);
    const auto input = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(field<std::uintptr_t>(vtable, 56))(screen);
    if (!input) return original(child);
    if (!opened) {
        flushPreferences(); // Last profile's pending record is written only to its own key.
        opened = true;
        const auto current = nativePattern(screen);
        SavedPreferences preferences{};
        if (current >= 6 && current <= 9) preferences.stockPattern = static_cast<std::uint8_t>(current);
        const auto loaded = loadPreferences(preferences);
        if (loaded.changed) {
            state.load(preferences);
            dirty = false;
        }
        beginRestore(state.memory().columns ? 6 : state.memory().stockPattern, current);
        restoreItem = 0;
        const auto focus = field<std::int32_t>(controller, 140);
        SlotObservation observation;
        if (focus > 0 && readInventoryItem(screen, static_cast<std::uint32_t>(focus - 1), observation))
            restoreItem = observation.item;
        if (!restoring && state.memory().columns && !enter(screen, 0)) return original(child);
        refreshSortPanel(screen);
    }
    if (entryPending) {
        // Native sorting queues GameData writes. Do not bind the post-sort shortcut
        // order to a partially updated inventory in the same update that sorted it.
        ++entryAttempts;
        if (!enter(screen, entryItem, true)) {
            if (!failed && entryAttempts >= 8) fail(screen, "inventory did not settle after sort");
            InputMask masked(input, stockDirections | 0x18u);
            return original(child); // Cancel/close still works; no action on an invalid snapshot.
        }
    }
    if (restoring) {
        // Replay one ordinary native sort transition per update. This respects unlocks,
        // native GameData queue timing and all other mods' ordering hooks.
        InputMask masked(input, stockDirections | 0x18u);
        field<std::uint32_t>(input, 8) |= 0x10u;
        observedSort = 0;
        watchSort = true;
        const auto result = original(child);
        watchSort = false;
        ++restoreAttempts;
        const bool known = observedSort >= 6 && observedSort <= 9;
        const auto bit = known ? 1u << (observedSort - 6) : 0;
        const bool reached = observedSort == restoreTarget;
        if (reached || !known || (restoreVisited & bit) || restoreAttempts >= 4) {
            restoring = false;
            if (!reached) Logging.Log("[material-columns] native mode restore unavailable desired=%u observed=%u attempts=%u",
                                      restoreTarget, observedSort, restoreAttempts);
            if (known && !state.memory().columns) {
                dirty |= observedSort != state.memory().stockPattern;
                state.setStockPattern(observedSort);
            }
        }
        restoreVisited |= bit;
        if (!restoring && state.memory().columns) scheduleEntry(0);
        else if (restoreItem) {
            std::size_t count = 0;
            if (readInventory(screen, entries.data(), entries.size(), count)) {
                for (std::size_t i = 0; i < count; ++i) if (entries[i].id == restoreItem && entries[i].eligible) {
                    focusNativeItem(screen, entries[i].native_slot, true);
                    break;
                }
            }
        }
        return result;
    }
    const auto pressed = field<std::uint32_t>(input, 8);
    // The stock X action takes precedence over Y in the native update.
    const bool sortPressed = (pressed & 0x18u) == 0x10u;
    if (!active) {
        observedSort = 0;
        watchSort = sortPressed;
        const auto result = original(child);
        watchSort = false;
        if (observedSort) { state.setStockPattern(observedSort); dirty = true; }
        // Native unlock rules may omit a stock mode; its actual wrap to By type is decisive.
        if (observedSort == 6 && field<std::uint8_t>(child, 161)) scheduleEntry(0, true);
        if (sortPressed) flushPreferences();
        return result;
    }
    if (sortPressed) {
        state.setColumns(false);
        state.setStockPattern(6);
        dirty = true;
        active = false;
        navigation.reset();
        hideColumnPanel(screen);
        InputMask masked(input, 0x10u | stockDirections);
        const auto result = original(child);
        field<std::uint8_t>(child, 161) = 1; // Let the parent refresh the native sort panel.
        beginRestore(6, nativePattern(screen));
        restoreItem = state.selected() ? state.selected()->id : 0;
        flushPreferences();
        return result;
    }

    const auto move = movement(input, controller);
    bool refresh = field<std::int32_t>(controller, 136) - 1 != snapshotCount || move.axis != Axis::none;
    if (const auto* item = state.selected()) {
        SlotObservation current;
        if (!readInventoryItem(screen, item->native_slot, current) || !current.eligible || current.item != item->id)
            refresh = true;
        else if (current.quantity != selectedQuantity) {
            selectedQuantity = current.quantity;
            refresh = true;
        }
    }
    if (refresh && !rebuild(screen)) { fail(screen, "refresh"); return original(child); }
    const bool moved = move.axis == Axis::horizontal ? state.horizontal(move.direction) :
                       move.axis == Axis::vertical && state.vertical(move.direction);
    if (moved && move.axis == Axis::vertical) animateColumnMove(screen, state.focused(), move.direction);
    if (refresh && !display(screen)) { fail(screen, "refresh binding"); return original(child); }
    if (moved) { playColumnMoveSound(screen, move.direction > 0); dirty = true; idleFrames = 0; }
    else if (dirty) {
        idleFrames += frameStep();
        if (idleFrames >= field<std::uint8_t>(controller, 196)) flushPreferences();
    }
    if (const auto* item = state.selected()) {
        if (!focusNativeItem(screen, item->native_slot, false)) {
            fail(screen, "action index"); return original(child);
        }
    } else {
        // Native blank slot is the safe action target for an entirely ineligible inventory.
        field<std::int32_t>(controller, 140) = 0;
    }
    // Native drop/throw/arrow/cancel paths execute once with the real stock action child.
    // Restore the input bits before any other screen/consumer observes this frame.
    InputMask masked(input, stockDirections | 0x10u);
    return original(child);
}
void forgetMaterialScreen(std::uintptr_t screen) {
    if (owner != screen) return;
    flushPreferences();
    owner = 0;
    active = opened = failed = watchSort = restoring = entryPending = entryFromBeginning = false;
    snapshotCount = -1;
    navigation.reset();
}
bool materialColumnsActive(std::uintptr_t screen) { return owner == screen && active; }
} // namespace material_columns::native
