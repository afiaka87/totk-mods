// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "PrivateEffects.hpp"
#include "PrivateEffectResource.hpp"
#include "PrivateEffectBanks.hpp"
#include "PrivateEffectInfo.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <array>
#include <atomic>
#include <nn/fs.h>

namespace botw_abilities::effects {
namespace {
using totk::engine::readMemory;
using totk::engine::isPlausibleAddress;
constexpr std::uintptr_t kLoadResource = 0xF29A64, kELinkVersion = 0x2A5F778;
constexpr std::uintptr_t kLookupString = 0x775954, kEffectModule = 0x47240C8;
constexpr totk::engine::CodeSite kSites[]{
    {kLoadResource, 0xA9BF7BFD, 0x910003FD}, {kELinkVersion, 0x52800480, 0xD65F03C0},
    {kLookupString, 0xD10243FF, 0xA9037BFD}};
constexpr std::uintptr_t kEffectManagerOffset = 0xF0, kBankDictionaryOffset = 0x120;
constexpr char kPath[] = "content:/BotwAbilities/Effects/Private.belnk";
static_assert(kPrivateEffectBytes < 64 * 1024);
// Native relocation writes pointers into these bytes and retains them for the process lifetime.
alignas(16) std::array<std::uint8_t, kPrivateEffectBytes> g_bytes;
std::atomic_flag g_attempted{};
std::uintptr_t g_base{};

bool readResource() {
    nn::fs::FileHandle file{};
    auto result = nn::fs::OpenFile(&file, kPath, nn::fs::OpenMode_Read);
    s64 size = 0;
    bool valid = result.IsSuccess();
    if (valid) {
        result = nn::fs::GetFileSize(&size, file);
        valid = result.IsSuccess() && size == s64(g_bytes.size());
        if (valid) {
            result = nn::fs::ReadFile(file, 0, g_bytes.data(), g_bytes.size());
            valid = result.IsSuccess();
        }
        nn::fs::CloseFile(file);
    }
    valid = valid && validResource(g_bytes, kPrivateEffectChecksum);
    if (!valid)
        Logging.Log("[botw-effects] FILE_REFUSED result=%08x bytes=%lld expected=%u checksum=%08x",
                    result.GetInnerValueForDebug(), static_cast<long long>(size), unsigned(g_bytes.size()),
                    fingerprint(g_bytes));
    return valid;
}

HOOK_DEFINE_TRAMPOLINE(LoadResource) {
    static bool Callback(std::uintptr_t system, void* binary) {
        const bool baseLoaded = Orig(system, binary);
        if (!isPlausibleAddress(system)) return baseLoaded;
        const auto vtable = readMemory<std::uintptr_t>(system);
        if (!isPlausibleAddress(vtable) || readMemory<std::uintptr_t>(vtable + 0x40) != g_base + kELinkVersion)
            return baseLoaded;
        if (g_attempted.test_and_set(std::memory_order_relaxed)) return baseLoaded;
        const auto buffer = readMemory<std::uintptr_t>(system + 8);
        if (!baseLoaded || !isPlausibleAddress(buffer)) {
            Logging.Log("[botw-effects] BASE_REFUSED loaded=%u buffer=%p", unsigned(baseLoaded),
                        reinterpret_cast<void*>(buffer));
            return baseLoaded;
        }
        const auto loaded = readMemory<int>(buffer + 8);
        const auto count = readMemory<int>(buffer + 0x10);
        const auto capacity = readMemory<int>(buffer + 0x14);
        if (!hasResourceSlot(loaded, count, capacity)) {
            Logging.Log("[botw-effects] SLOT_REFUSED loaded=%d count=%d capacity=%d", loaded, count, capacity);
            return baseLoaded;
        }
        if (!readResource()) return baseLoaded;
        const bool attached = Orig(system, g_bytes.data());
        Logging.Log("[botw-effects] PRIVATE_RESOURCE loaded=%u bytes=%u buffers=%d->%d capacity=%d globals=%u",
                    unsigned(attached), unsigned(g_bytes.size()), count, readMemory<int>(buffer + 0x10),
                    capacity, unsigned(readMemory<std::uint8_t>(system + 0x60)));
        return baseLoaded;
    }
};

HOOK_DEFINE_TRAMPOLINE(LookupBank) {
    static bool Callback(std::uintptr_t dictionary, const char** value, const char* key) {
        const bool found = Orig(dictionary, value, key);
        if (found || !value || !key) return found;
        const auto module = readMemory<std::uintptr_t>(g_base + kEffectModule);
        if (!isPlausibleAddress(module)) return false;
        const auto manager = readMemory<std::uintptr_t>(module + kEffectManagerOffset);
        if (!isPlausibleAddress(manager)) return false;
        const auto bank = missingPrivateBank(dictionary, manager + kBankDictionaryOffset, key);
        if (!bank) return false;
        *value = bank;
        Logging.Log("[botw-effects] PRIVATE_BANK user=%s bank=%s", key, bank);
        return true;
    }
};
}

bool install(std::uintptr_t base) {
    if (!totk::engine::codeMatches(base, kSites, "botw-effects", "SIGNATURE_REFUSED")) return false;
    g_base = base;
    LookupBank::InstallAtOffset(kLookupString);
    LoadResource::InstallAtOffset(kLoadResource);
    return true;
}
}
