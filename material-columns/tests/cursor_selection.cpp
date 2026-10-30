// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_PANEL_HOST_TEST
#include <cassert>
#include <cstdint>
std::uintptr_t nativeNodeMovable(std::uintptr_t, unsigned);
struct CursorTestHook {
    static std::uintptr_t Orig(std::uintptr_t node, unsigned target) { return nativeNodeMovable(node, target); }
    template<class... Args> static std::uintptr_t Orig(Args...) { return 0; }
    static void InstallAtOffset(std::uintptr_t) {}
};
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : CursorTestHook
#include "../src/native/ColumnPanel.cpp"
using namespace material_columns::native;
namespace {
alignas(8) std::array<std::byte, 512> shortcut{}, otherScreen{};
alignas(8) std::array<std::array<std::byte, 72>, 4> nodes{};
unsigned nativeQueries{};
template<class T> std::uintptr_t address(T& value) { return reinterpret_cast<std::uintptr_t>(value.data()); }
struct Cursor {
    std::uintptr_t active{}, requested{};
    bool frameAttached{};
    unsigned deactivations{};
    void select(std::uintptr_t node) {
        if (active && active != node) ++deactivations;
        active = node;
    }
    void update(int direction, unsigned target) {
        // BoxCursorControl::Update first detaches its frame, rejects an invalid
        // current node, then On searches eligible nodes before attaching again.
        frameAttached = false;
        if (!active || !ShortcutCursorFocus::Callback(active, target)) {
            std::uintptr_t next{};
            for (auto& node : nodes) if (ShortcutCursorFocus::Callback(address(node), target)) {
                next = address(node); break;
            }
            select(next);
        }
        if (requested && ShortcutCursorFocus::Callback(requested, target)) select(requested);
        requested = 0;
        if (active && direction) {
            const auto neighbor = field<std::uintptr_t>(active, direction < 0 ? 40 : 48);
            if (neighbor && ShortcutCursorFocus::Callback(neighbor, target)) select(neighbor);
        }
        frameAttached = active != 0;
    }
};
}
std::uintptr_t nativeNodeMovable(std::uintptr_t node, unsigned target) {
    ++nativeQueries;
    // Simulated native visibility/draw-target eligibility, independent of ownership.
    return node && field<unsigned>(node, 8) && field<unsigned>(node, 12) == target;
}
namespace material_columns::native {
std::uintptr_t updateMaterialShortcut(std::uintptr_t child, NativeUpdate original) { return original(child); }
void forgetMaterialScreen(std::uintptr_t) {}
namespace {
std::uintptr_t resolvePanelTestFunction(std::uintptr_t) { assert(false); return 0; }
}
}
int main() {
    panels[0].owner = address(shortcut);
    for (unsigned i = 0; i < nodes.size(); ++i) {
        const auto node = address(nodes[i]);
        field<std::uintptr_t>(node, 32) = i < 3 ? address(shortcut) : address(otherScreen);
        field<std::uintptr_t>(node, 40) = address(nodes[(i + 3) % 4]);
        field<std::uintptr_t>(node, 48) = address(nodes[(i + 1) % 4]);
    }
    for (unsigned type = 0; type < 4; ++type) for (bool columns : {false, true}) {
        field<unsigned>(address(shortcut), 440) = type;
        panels[0].visible = columns;
        panels[0].ready = columns;
        for (unsigned target = 0; target < 2; ++target) {
            for (unsigned i = 0; i < nodes.size(); ++i) {
                field<unsigned>(address(nodes[i]), 8) = i < 3;
                field<unsigned>(address(nodes[i]), 12) = target;
            }
            Cursor cursor{address(nodes[0]), address(nodes[1]), true};
            for (int direction : {-1, 0, 1}) for (unsigned frame = 0; frame < 120; ++frame) {
                cursor.requested = address(nodes[1]);
                cursor.update(direction, target);
                assert(!cursor.active && !cursor.frameAttached);
            }
            assert(cursor.deactivations == 1);
            // Opening another menu must acquire and draw its cursor normally.
            field<unsigned>(address(nodes[3]), 8) = 1;
            cursor.update(0, target);
            assert(cursor.active == address(nodes[3]) && cursor.frameAttached);
            cursor.requested = address(nodes[1]);
            cursor.update(-1, target);
            assert(cursor.active == address(nodes[3]) && cursor.frameAttached);
            assert(!ShortcutCursorFocus::Callback(address(nodes[3]), target ^ 1));
        }
    }
    assert(nativeQueries > 0);
    // Screen destruction removes ownership; no persistent global cursor disable.
    panels[0] = {};
    assert(ShortcutCursorFocus::Callback(address(nodes[0]), 1));
    assert(!ShortcutCursorFocus::Callback(0, 1));
}
