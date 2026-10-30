// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_HOST_TEST
#include "../src/native/Runtime.cpp"
#include <cassert>
#include <vector>
#include <cstring>
using namespace material_columns;

namespace material_columns::native {
namespace {
bool testOpen = true, panelVisible = false;
int inventoryReads = 0, soundCount = 0, originalCalls = 0, actionSlot = -1;
bool simulateSort = false;
bool sortingInProgress = false;
int transientSnapshots = 0;
unsigned testPattern = 6;
bool profileChanged = true, saveWorks = true, diskHasRecord = false;
int saves = 0;
SavedPreferences diskState{};
std::array<InventoryEntry, 4> testInventory{{{10, 0, false, true}, {11, 1, false, true},
    {12, 2, false, true}, {20, 3, false, true}}};
std::array<std::byte, 512> screenBytes{}, childBytes{}, controlBytes{}, inputBytes{}, layoutBytes{}, fadeBytes{}, tableBytes{};
auto screenAddress() { return reinterpret_cast<std::uintptr_t>(screenBytes.data()); }
auto childAddress() { return reinterpret_cast<std::uintptr_t>(childBytes.data()); }
auto controllerAddress() { return reinterpret_cast<std::uintptr_t>(controlBytes.data()); }
auto inputAddress() { return reinterpret_cast<std::uintptr_t>(inputBytes.data()); }
unsigned isOpen(std::uintptr_t) { return testOpen; }
unsigned getPattern(std::uintptr_t, int kind) { assert(kind == 3); return testPattern; }
std::uintptr_t getInput(std::uintptr_t) { return inputAddress(); }
std::uintptr_t resolveTestFunction(std::uintptr_t offset) {
    if (offset == 0x10da870) return reinterpret_cast<std::uintptr_t>(&isOpen);
    assert(offset == 0x1acf8ac);
    return reinterpret_cast<std::uintptr_t>(&getPattern);
}
std::uintptr_t originalUpdate(std::uintptr_t) {
    ++originalCalls;
    const auto pressed = field<std::uint32_t>(inputAddress(), 8);
    if (testOpen && (simulateSort || restoring) && (pressed & 0x10)) {
        assert(watchSort);
        testPattern = restoring ? (testPattern == 9 ? 6 : testPattern + 1) : 6;
        sortingInProgress = true;
        observedSort = testPattern; // native observer event; actual hook/ARM checked separately
        field<std::uint8_t>(childAddress(), 161) = 1;
        std::swap(testInventory[0].native_slot, testInventory[2].native_slot);
        field<std::int32_t>(controllerAddress(), 140) = 1; // native sort resets focus
    }
    if (pressed & 8) actionSlot = field<std::int32_t>(controllerAddress(), 140) - 1;
    if (materialColumnsActive(screenAddress())) {
        assert(!(pressed & stockDirections));
        assert(!(field<std::uint32_t>(inputAddress(), 276) & stockDirections));
    }
    return 123;
}
}
bool initializePreferenceStorage(std::uintptr_t, bool) { return true; }
PreferenceLoad loadPreferences(SavedPreferences& value) {
    if (!profileChanged) return {false, true};
    profileChanged = false;
    if (diskHasRecord) value = diskState;
    return {true, true};
}
bool savePreferences(const SavedPreferences& value) {
    ++saves;
    if (!saveWorks) return false;
    diskState = value;
    diskHasRecord = true;
    return true;
}
bool columnPanelReady(std::uintptr_t) { return true; }
void refreshSortPanel(std::uintptr_t) {}
void animateColumnMove(std::uintptr_t, std::size_t, int) {}
bool configureColumnPanel(std::uintptr_t, std::size_t count, std::size_t focus, BindColumnItem callback, void* context, DescribeColumn describe) {
    assert(count <= maximumColumns);
    panelVisible = true;
    if (!count) return true;
    assert(describe(focus).label);
    std::uint32_t slot = 999; bool selected = false;
    assert(callback(context, static_cast<std::uint32_t>(focus * 3), slot, selected) && selected);
    return true;
}
void hideColumnPanel(std::uintptr_t) { panelVisible = false; }
bool focusNativeItem(std::uintptr_t, std::uint32_t slot, bool) {
    assert(slot < testInventory.size());
    field<std::int32_t>(controllerAddress(), 140) = static_cast<int>(slot + 1);
    return true;
}
void playColumnMoveSound(std::uintptr_t, bool) { ++soundCount; }
bool readInventory(std::uintptr_t, InventoryEntry* output, std::size_t capacity, std::size_t& count) {
    ++inventoryReads;
    assert(capacity >= testInventory.size());
    std::copy(testInventory.begin(), testInventory.end(), output);
    count = testInventory.size();
    if (sortingInProgress || transientSnapshots > 0) {
        // Invalid adapter snapshot: two entries claim the same action slot.
        // Repeated actor names alone are valid and must not block column entry.
        output[1].native_slot = output[0].native_slot;
        if (transientSnapshots > 0) --transientSnapshots;
    }
    return true;
}
bool readInventoryItem(std::uintptr_t, std::uint32_t slot, SlotObservation& result) {
    for (const auto& item : testInventory) if (item.native_slot == slot) {
        result = {item.id, 7, true, item.eligible};
        return true;
    }
    return false;
}
}

int main() {
    using namespace material_columns::native;
    // A fake mapped-global region lets the real double-dereference clock reader execute.
    std::vector<std::byte> globals(0x462df08 + sizeof(std::uintptr_t));
    base = reinterpret_cast<std::uintptr_t>(globals.data());
    std::array<std::byte, 16> clock{};
    std::uintptr_t clockPointer = reinterpret_cast<std::uintptr_t>(clock.data());
    field<float>(clockPointer, 8) = 1;
    field<std::uintptr_t>(base, 0x462df08) = reinterpret_cast<std::uintptr_t>(&clockPointer);
    installed = true;
    const auto screen = screenAddress(), child = childAddress(), control = controllerAddress(), input = inputAddress();
    field<std::uintptr_t>(screen, 0) = reinterpret_cast<std::uintptr_t>(tableBytes.data());
    field<std::uintptr_t>(field<std::uintptr_t>(screen, 0), 56) = reinterpret_cast<std::uintptr_t>(&getInput);
    field<std::uintptr_t>(screen, 424) = child;
    field<std::uint32_t>(screen, 440) = 3;
    field<std::uintptr_t>(child, 24) = screen;
    field<std::uintptr_t>(child, 80) = control;
    field<std::uintptr_t>(child, 32) = reinterpret_cast<std::uintptr_t>(layoutBytes.data());
    field<std::uintptr_t>(child, 48) = reinterpret_cast<std::uintptr_t>(fadeBytes.data());
    field<std::uint8_t>(field<std::uintptr_t>(child, 32), 145) = 2;
    field<std::int32_t>(control, 136) = 5;
    field<std::int32_t>(control, 140) = 4; // last family (item20) before native reorder
    field<std::uint8_t>(control, 196) = 12;
    field<std::uint8_t>(control, 197) = 3;
    field<std::int32_t>(input, 264) = 24;
    auto tick = [&] { assert(updateMaterialShortcut(child, originalUpdate) == 123); sortingInProgress = false; };
    tick();
    assert(originalCalls == 1 && !panelVisible && !materialColumnsActive(screen));
    simulateSort = true;
    field<std::uint32_t>(input, 8) = 0x10;
    const auto readsBeforeSort = inventoryReads;
    tick();
    assert(entryPending && !failed && !materialColumnsActive(screen));
    assert(inventoryReads == readsBeforeSort); // Never snapshot inside the native sort update.
    field<std::uint32_t>(input, 8) = 8; // No action while a retry snapshot remains inconsistent.
    transientSnapshots = 1;
    tick();
    assert(entryPending && !failed && !materialColumnsActive(screen) && actionSlot == -1);
    assert(field<std::uint32_t>(input, 8) == 8);
    field<std::uint32_t>(input, 8) = 0;
    tick();
    assert(materialColumnsActive(screen) && panelVisible && state.selected()->id == 10);
    assert(state.selected()->native_slot == 2 && field<int>(control, 140) == 3);
    field<std::uint32_t>(input, 8) = 0;
    tick();
    const auto reads = inventoryReads;
    for (int i = 0; i < 20; ++i) tick();
    assert(inventoryReads == reads); // idle frames must not rescan every material
    field<float>(input, 300) = -1;
    field<std::uint32_t>(input, 276) = 1u << 25;
    field<std::uint32_t>(input, 8) = 8;
    tick();
    assert(state.selected()->id == 11 && actionSlot == 1 && soundCount == 1);
    assert(field<std::uint32_t>(input, 276) == (1u << 25) && field<std::uint32_t>(input, 8) == 8);
    // Exhaustion must choose a valid neighbor before the native action reads focus.
    testInventory[1].eligible = false;
    field<std::uint32_t>(input, 276) = 0;
    tick();
    assert(state.selected()->id == 10 && actionSlot == 2);
    assert(state.memory().items[0].item == 11); // temporary fallback is not a new preference
    // Cancellation closes the view; reopening restores its mode and item memory.
    field<std::uint32_t>(input, 8) = 0;
    testOpen = false;
    tick();
    assert(!panelVisible && state.memory().columns);
    assert(diskState.columns && diskState.items[0].item == 11 && !dirty);
    testInventory[1].eligible = true;
    testOpen = true;
    tick();
    assert(materialColumnsActive(screen) && state.selected()->id == 11);
    field<std::uint32_t>(input, 8) = 0x10;
    tick();
    assert(!materialColumnsActive(screen) && !panelVisible && !state.memory().columns);
    assert(field<std::uint32_t>(input, 8) == 0x10);
    assert(field<int>(control, 140) == 2); // leaving columns preserves exact stock item
    forgetMaterialScreen(screen);
    assert(!materialColumnsActive(screen) && owner == 0);

    // A restart/profile change restores the saved stock mode through ordinary native
    // transitions, one original update per frame; focused actor survives each sort.
    field<std::uint32_t>(input, 8) = 0;
    diskState = {};
    diskState.stockPattern = 9;
    diskState.columns = false;
    profileChanged = true;
    testPattern = 6;
    const int callsBeforeRestore = originalCalls;
    tick(); assert(restoring && testPattern == 7);
    tick(); assert(restoring && testPattern == 8);
    tick(); assert(!restoring && testPattern == 9 && !panelVisible);
    assert(originalCalls == callsBeforeRestore + 3);
    assert(field<std::uint32_t>(input, 8) == 0 && state.memory().stockPattern == 9);
    const auto saveBaseline = saves;
    for (int i = 0; i < 20; ++i) tick();
    assert(saves == saveBaseline);

    // Columns restores its private mode after bringing the native inventory back
    // to By type. Cancellation flushes a pending move even before the idle delay.
    forgetMaterialScreen(screen);
    diskState.columns = true;
    profileChanged = true;
    tick();
    assert(!restoring && testPattern == 6 && entryPending && !materialColumnsActive(screen));
    tick();
    assert(materialColumnsActive(screen));
    field<float>(input, 300) = -1;
    field<std::uint32_t>(input, 276) = 1u << 25;
    tick();
    assert(dirty);
    saveWorks = false;
    testOpen = false;
    tick();
    assert(dirty); // failed writes are not treated as durable
    saveWorks = true;
    testOpen = true;
    field<std::uint32_t>(input, 276) = 0;
    tick();
    assert(!dirty && diskState.columns && materialColumnsActive(screen));
    forgetMaterialScreen(screen);
    diskState.columns = false;
    diskState.stockPattern = 6;
    profileChanged = true;
    field<std::uint32_t>(input, 8) = 0;
    tick();
    field<std::uint32_t>(input, 8) = 0x10;
    tick();
    assert(entryPending);
    testOpen = false;
    tick();
    assert(!entryPending && !panelVisible);
    testOpen = true;
    field<std::uint32_t>(input, 8) = 0;
    tick();
    field<std::uint32_t>(input, 8) = 0x10;
    tick();
    assert(entryPending);
    field<std::uint32_t>(input, 8) = 0;
    transientSnapshots = 20;
    for (int i = 0; i < 8; ++i) tick();
    assert(failed && !entryPending && !materialColumnsActive(screen) && !panelVisible);
    forgetMaterialScreen(screen);
    // Regression for the observed repeated type: entry and actions must use distinct slots.
    transientSnapshots = 0;
    testInventory = {{{10, 0, true, true}, {10, 1, true, true},
                      {12, 2, false, true}, {20, 3, false, true}}};
    diskState = {};
    diskState.stockPattern = 6;
    profileChanged = true;
    field<float>(input, 300) = 0;
    field<std::uint32_t>(input, 8) = 0;
    tick();
    field<std::int32_t>(control, 140) = 1;
    field<std::uint32_t>(input, 8) = 0x10;
    tick();
    assert(entryPending);
    field<std::uint32_t>(input, 8) = 0;
    tick();
    assert(materialColumnsActive(screen) && state.selected()->id == 10);
    const auto firstSlot = state.selected()->native_slot;
    field<float>(input, 300) = -1;
    field<std::uint32_t>(input, 276) = 1u << 25;
    field<std::uint32_t>(input, 8) = 8;
    tick();
    assert(state.selected()->id == 10 && state.selected()->native_slot != firstSlot);
    const auto repeatedSlot = state.selected()->native_slot;
    assert(actionSlot == static_cast<int>(repeatedSlot));
    field<std::uint32_t>(input, 276) = 0;
    snapshotCount = -1; // Force a refresh while acting on the second occurrence.
    tick();
    assert(state.selected()->native_slot == repeatedSlot && actionSlot == static_cast<int>(repeatedSlot));
    field<std::uint32_t>(input, 8) = 0x10;
    tick();
    assert(!materialColumnsActive(screen) && !panelVisible);
    forgetMaterialScreen(screen);
}
