// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "engine/LinkAudio.hpp"

#include <lib.hpp>

namespace linked_stick::engine::audio {
namespace {
// TotK 1.2.1 sound path: Bivouac CampAudio, observed in v0.25.0.
constexpr std::ptrdiff_t kSearchAndEmit = 0x00B026E0;
constexpr std::ptrdiff_t kSystemSlot = 0x0462F2B0;
constexpr std::ptrdiff_t kHandleIsValid = 0x00D17C80;
std::uintptr_t base = 0;

bool pointer(std::uintptr_t p) {
    return p >= 0x1000000 && p < 0x8000000000ull && (p & 3) == 0;
}

bool interfaceName(const char* name) {
    constexpr char wanted[] = "UI_GlobalSound";
    for (unsigned i = 0; i < sizeof(wanted); ++i) {
        if (name[i] != wanted[i]) return false;
    }
    return true;
}

void* speaker() {
    if (!base) return nullptr;
    const auto holder = *reinterpret_cast<const std::uintptr_t*>(base + kSystemSlot);
    if (!pointer(holder)) return nullptr;
    const auto system = *reinterpret_cast<const std::uintptr_t*>(holder);
    if (!pointer(system) || !*reinterpret_cast<const unsigned*>(system + 32)) return nullptr;
    const int nodeOffset = *reinterpret_cast<const int*>(system + 36);
    const auto anchor = system + 16;
    auto node = *reinterpret_cast<const std::uintptr_t*>(system + 24);
    for (unsigned guard = 0; guard < 4096 && node != anchor; ++guard) {
        if (!pointer(node)) return nullptr;
        const auto user = node - static_cast<std::uintptr_t>(nodeOffset);
        if (!pointer(user)) return nullptr;
        const auto name = *reinterpret_cast<const std::uintptr_t*>(user + 16);
        if (pointer(name) && interfaceName(reinterpret_cast<const char*>(name))) {
            if (*reinterpret_cast<const int*>(user + 48) < 1) return nullptr;
            const auto head = *reinterpret_cast<const std::uintptr_t*>(user + 32);
            if (head == user + 32 || !pointer(head)) return nullptr;
            const auto instance = head - static_cast<std::uintptr_t>(
                *reinterpret_cast<const int*>(user + 52));
            if (!pointer(instance)) return nullptr;
            const auto owner = *reinterpret_cast<const std::uintptr_t*>(instance + 0x58);
            if (!pointer(owner) || !pointer(*reinterpret_cast<const std::uintptr_t*>(owner + 0x18)))
                return nullptr;
            return reinterpret_cast<void*>(instance);
        }
        node = *reinterpret_cast<const std::uintptr_t*>(node + 8);
    }
    return nullptr;
}
}

void initialize(std::uintptr_t mainBase) { base = mainBase; }

bool play(const char* cue) {
    if (!cue) return false;
    void* instance = speaker();
    if (!instance) {
        Logging.Log("[fleet-sync] AUDIO cue=%s requested=1 emitted=0 reason=no_ui_speaker", cue);
        return false;
    }
    alignas(8) std::uint8_t handle[16] = {0, 0, 0xFF, 0xFF};
    using Emit = void (*)(void*, const char*, void*);
    using Valid = bool (*)(const void*);
    reinterpret_cast<Emit>(base + kSearchAndEmit)(instance, cue, handle);
    const bool emitted = reinterpret_cast<Valid>(base + kHandleIsValid)(handle);
    Logging.Log("[fleet-sync] AUDIO cue=%s requested=1 emitted=%u", cue, emitted ? 1u : 0u);
    return emitted;
}
}
