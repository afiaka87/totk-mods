// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis
#pragma once
#include "totk/ui/NativeFont.hpp"

namespace totk::ui::native_font {
struct Services {
    void* context = nullptr;
    bool (*read)(void*, std::uintptr_t, void*, std::size_t) = nullptr;
    void (*diagnostic)(void*, const char*) = nullptr;
    // Optional host-test seam; production leaves this null and resolves main + offset.
    void* (*resolve)(void*, std::uintptr_t) = nullptr;
};
using UiCallback = std::uint64_t (*)(void*, void*);
using HookInstaller = bool (*)(void*, std::uintptr_t, UiCallback, UiCallback&);

// Source-level process singleton. No TextWriter, debug-font loader, heap steal or texture ID use. configure/install once before drawing. publish/clear: one producer thread only. drawUi: one native UI consumer thread. A diagnostic may produce on that same thread.
bool configure(std::uintptr_t mainBase, Atlas atlas, Services services) noexcept;
bool installUiHook(HookInstaller installer, void* context) noexcept;
bool publish(const Frame&) noexcept;
void clear() noexcept;
void drawUi(std::uintptr_t module, std::uintptr_t args) noexcept;
} // namespace totk::ui::native_font
