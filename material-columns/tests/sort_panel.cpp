// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <lib.hpp>
#include <array>
#include <cassert>
#include <cstring>
struct SetSortCountBase {
    inline static std::uintptr_t (*Orig)(std::uintptr_t, int){};
    static void InstallAtOffset(std::uintptr_t) {}
};
struct SetSortSelectionBase {
    inline static std::uintptr_t (*Orig)(std::uintptr_t, unsigned, bool){};
    static void InstallAtOffset(std::uintptr_t) {}
};
struct SetSortMessageBase {
    inline static std::uintptr_t (*Orig)(std::uintptr_t, const char*, const void*, bool, std::uintptr_t){};
    static void InstallAtOffset(std::uintptr_t) {}
};
#undef HOOK_DEFINE_TRAMPOLINE
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : Name##Base
#define MATERIAL_COLUMNS_HOST_TEST
#include "../src/native/SortPanel.cpp"

namespace material_columns::native {
namespace {
alignas(8) std::array<std::byte, 512> screenBytes{}, partsBytes{}, layoutBytes{}, unrelatedLayout{};
alignas(8) std::array<std::array<std::byte, 240>, 5> dotBytes{};
std::uintptr_t screen = reinterpret_cast<std::uintptr_t>(screenBytes.data());
std::uintptr_t parts = reinterpret_cast<std::uintptr_t>(partsBytes.data());
std::uintptr_t layout = reinterpret_cast<std::uintptr_t>(layoutBytes.data());
bool active = false;
unsigned pattern = 6, selected = 99;
int dots = 0, textCalls = 0;
const void* observedMessage{};
template<class T> void put(std::uintptr_t object, std::size_t offset, T value) {
    std::memcpy(reinterpret_cast<void*>(object + offset), &value, sizeof(value));
}
std::uintptr_t countOriginal(std::uintptr_t, int count) { dots = count; return 12; }
std::uintptr_t selectionOriginal(std::uintptr_t, unsigned value, bool) { selected = value; return 13; }
std::uintptr_t messageOriginal(std::uintptr_t, const char*, const void* message, bool, std::uintptr_t) {
    ++textCalls; observedMessage = message; return 14;
}
unsigned getPattern(std::uintptr_t, int kind) { assert(kind == 3); return pattern; }
std::uintptr_t findPane(std::uintptr_t ownerLayout, const char* name) {
    assert(ownerLayout == layout && !std::strncmp(name, "Pa_Page_0", 9));
    const auto i = name[9] - '0';
    assert(i >= 0 && i < 5);
    return reinterpret_cast<std::uintptr_t>(dotBytes[i].data());
}
std::uintptr_t resolveTestFunction(std::uintptr_t offset) {
    switch (offset) {
    case 0x1acf8ac: return reinterpret_cast<std::uintptr_t>(&getPattern);
    case 0x7012ec: return reinterpret_cast<std::uintptr_t>(&findPane);
    case 0x1ad000c: return reinterpret_cast<std::uintptr_t>(&SetSortCount::Callback);
    case 0x1ad0084: return reinterpret_cast<std::uintptr_t>(&SetSortSelection::Callback);
    case 0xb519b0: return reinterpret_cast<std::uintptr_t>(&SetSortMessage::Callback);
    default: assert(false); return 0;
    }
}
}
bool columnPanelReady(std::uintptr_t owner) { return owner == screen; }
bool materialColumnsActive(std::uintptr_t owner) { return owner == screen && active; }
}
int main() {
    using namespace material_columns::native;
    SetSortCount::Orig = countOriginal;
    SetSortSelection::Orig = selectionOriginal;
    SetSortMessage::Orig = messageOriginal;
    put(parts, 24, screen); put(parts, 32, layout); put(parts, 72, 5);
    put(screen, 432, parts); put(screen, 440, 3u); put(layout, 128, screen);
    installed = true;
    for (unsigned p = 6; p <= 9; ++p) {
        pattern = p;
        refreshSortPanel(screen);
        assert(dots == 5 && selected == p - 6);
        for (unsigned i = 0; i < 5; ++i) {
            const auto pane = reinterpret_cast<std::uintptr_t>(dotBytes[i].data());
            assert(field<float>(pane, 48) == i * 18.f - 36.f);
        }
    }
    active = true;
    refreshSortPanel(screen);
    assert(selected == 4 && textCalls == 2 && observedMessage == &label);
    int nativeMessage = 0;
    SetSortMessage::Callback(layout, "T_Sort_00", &nativeMessage, true, 0);
    assert(observedMessage == &label);
    SetSortMessage::Callback(layout, "T_Text_00", &nativeMessage, true, 0);
    assert(observedMessage == &nativeMessage);
    SetSortMessage::Callback(reinterpret_cast<std::uintptr_t>(unrelatedLayout.data()), "T_Sort_00", &nativeMessage, true, 0);
    assert(observedMessage == &nativeMessage);
    active = false;
    SetSortMessage::Callback(layout, "T_Sort_01", &nativeMessage, true, 0);
    assert(observedMessage == &nativeMessage);
    put(screen, 440, 1u); // Weapon shortcut retains its native single indicator.
    SetSortCount::Callback(parts, 1);
    SetSortSelection::Callback(parts, 0, false);
    assert(dots == 1 && selected == 0);
    const auto fifth = reinterpret_cast<std::uintptr_t>(dotBytes[4].data());
    assert(!(field<std::uint8_t>(fifth, 88) & 1));
}
