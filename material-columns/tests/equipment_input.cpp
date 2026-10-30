// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_PANEL_HOST_TEST
#include <cassert>
#include <cstdint>
std::uintptr_t nativeControlUpdate(std::uintptr_t, float);
struct InputTestHook {
    static std::uintptr_t Orig(std::uintptr_t control, float frames) { return nativeControlUpdate(control, frames); }
    template<class... Args> static std::uintptr_t Orig(Args...) { return 0; }
    static void InstallAtOffset(std::uintptr_t) {}
};
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : InputTestHook
#include "../src/native/ColumnPanel.cpp"
using namespace material_columns::native;
namespace {
alignas(8) std::array<std::byte, 512> screen{}, stock{}, control{}, privateControl{}, input{}, table{}, page{};
template<class T> std::uintptr_t address(T& value) { return reinterpret_cast<std::uintptr_t>(value.data()); }
int cursorDirection{};
unsigned nativeFrames{}, cursorMoves{};
std::uintptr_t getInput(std::uintptr_t) { return address(input); }
void queueCursor(std::uintptr_t target, unsigned route, int direction) {
    field<std::uint8_t>(target, 195) = 255;
    field<std::uintptr_t>(target, 208) = 0;
    field<std::uintptr_t>(target, 216) = 0;
    field<std::uint8_t>(target, 190) = 0x62;
    cursorDirection = direction;
    if (route == 0) field<std::uint8_t>(target, 195) = direction < 0 ? 4 : 6;
    if (route == 1) {
        field<std::uintptr_t>(target, 208) = address(page);
        field<std::uint8_t>(address(page), 60) = direction < 0 ? 1 : 2;
    }
    if (route == 2) field<std::uint8_t>(target, 190) |= 1;
}
}
std::uintptr_t nativeControlUpdate(std::uintptr_t target, float frames) {
    assert(frames == 1.f);
    ++nativeFrames;
    // F6E9C4's three cursor routes: pending page, queued direction, continued
    // BoxCursor movement. The shortcut's right-stick update runs separately.
    int move{};
    if (const auto pending = field<std::uintptr_t>(target, 208)) {
        if (pending != field<std::uintptr_t>(target, 216))
            move = field<std::uint8_t>(pending, 60) == 1 ? -1 : 1;
    } else if (const auto direction = field<std::uint8_t>(target, 195); direction != 255) {
        move = direction / 2 == 2 ? -1 : 1;
    } else if ((field<std::uint8_t>(target, 190) & 5) == 1) move = cursorDirection;
    if (move) { field<int>(target, 140) += move; ++cursorMoves; }
    // Existing index interpolation/animation must still execute.
    field<float>(target, 144) += (field<int>(target, 140) - field<float>(target, 144)) * .5f;
    return 73;
}
namespace material_columns::native {
std::uintptr_t updateMaterialShortcut(std::uintptr_t child, NativeUpdate original) { return original(child); }
void forgetMaterialScreen(std::uintptr_t) {}
namespace {
std::uintptr_t resolvePanelTestFunction(std::uintptr_t) { assert(false); return 0; }
}
}
int main() {
    const auto owner = address(screen), child = address(stock), controller = address(control);
    field<std::uintptr_t>(owner, 0) = address(table);
    field<std::uintptr_t>(address(table), 56) = reinterpret_cast<std::uintptr_t>(&getInput);
    field<std::uintptr_t>(owner, 424) = child;
    field<std::uintptr_t>(child, 80) = controller;
    panels[0].owner = owner;
    panels[0].controller = address(privateControl);
    // Real shortcut types: weapon=0, shield=1, bow=2. Exercise both D-pad
    // directions, all generic cursor routes and a long held-button repeat.
    for (unsigned type = 0; type < 3; ++type) for (int direction : {-1, 1}) {
        field<unsigned>(owner, 440) = type;
        field<unsigned>(address(input), 276) = direction < 0 ? 0x40000 : 0x80000;
        field<unsigned>(address(input), 8) = field<unsigned>(address(input), 276);
        for (unsigned route = 0; route < 3; ++route) {
            field<int>(controller, 140) = 20;
            for (unsigned frame = 0; frame < 240; ++frame) {
                queueCursor(controller, route, direction);
                const auto savedInput = input;
                assert(UpdateColumnController::Callback(controller, 1.f) == 73);
                assert(field<int>(controller, 140) == 20);
                assert(input == savedInput); // D-pad still opens/holds/closes the menu.
                assert((field<std::uint8_t>(controller, 190) & 0x62) == 0x62);
            }
            // Native 1ACCB60 writes the right-stick selection directly. A queued
            // opposing D-pad request must neither undo it nor add a second step.
            field<int>(controller, 140) = 20 + direction;
            field<float>(controller, 144) = 20.f;
            field<float>(address(input), 296) = float(direction);
            queueCursor(controller, route, -direction);
            assert(UpdateColumnController::Callback(controller, 1.f) == 73);
            assert(field<int>(controller, 140) == 20 + direction);
            assert(field<float>(controller, 144) == 20.f + direction * .5f);
            field<float>(address(input), 296) = 0;
        }
    }
    assert(cursorMoves == 0 && nativeFrames == 3 * 2 * 3 * 241);
    // Hidden/unready private list must also be input-inert. It previously fell
    // through to generic cursor processing outside column mode.
    for (unsigned route = 0; route < 3; ++route) {
        queueCursor(address(privateControl), route, 1);
        assert(UpdateColumnController::Callback(address(privateControl), 1.f) == 73);
        assert(field<int>(address(privateControl), 140) == 0);
    }
    // No global navigation filter: native materials and unrelated lists forward.
    field<unsigned>(owner, 440) = 3;
    queueCursor(controller, 0, 1);
    assert(UpdateColumnController::Callback(controller, 1.f) == 73);
    assert(cursorMoves == 1);
    panels[0] = {};
    queueCursor(controller, 0, -1);
    assert(UpdateColumnController::Callback(controller, 1.f) == 73);
    assert(cursorMoves == 2);
}
