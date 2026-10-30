// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace material_columns::native::memory {
#ifdef MATERIAL_COLUMNS_PANEL_HOST_TEST
inline void registerCapture(std::uintptr_t, std::uintptr_t) {}
inline void captureEnabled(std::uintptr_t, bool) {}
inline void forget(std::uintptr_t) {}
inline void menuClosed() {}
inline bool privateBinding(bool) { return false; }
struct HeapSample { HeapSample(std::uintptr_t, const char*, bool = false) {} };
#else
bool install(std::uintptr_t base, bool validateOnly = false);
void registerCapture(std::uintptr_t owner, std::uintptr_t pane);
void captureEnabled(std::uintptr_t pane, bool enabled);
void forget(std::uintptr_t owner);
void menuClosed();
bool privateBinding(bool value);
struct HeapSample {
    std::uintptr_t heap;
    const char* phase;
    std::uint64_t before;
    bool detail;
    HeapSample(std::uintptr_t heap, const char* phase, bool detail = false);
    ~HeapSample();
};
#endif
} // namespace material_columns::native::memory
