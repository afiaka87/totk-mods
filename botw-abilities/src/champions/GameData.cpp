// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "GameData.hpp"
#include "SaveBuffer.hpp"
#include "ChampionSaveSchema.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/GameData.hpp"
#include <lib.hpp>

namespace botw_abilities::champions::data {
namespace {
using namespace totk::engine;
constexpr std::uintptr_t kCalc = 0xEE9704, kSetSave = 0x1730EEC, kSerialize = 0x241469C;
constexpr CodeSite kSites[]{
    {kCalc, 0xA9BA7BFD, 0xA9016FFC}, {kSetSave, 0xB94CC008, 0x6B02011F},
    {kSerialize, 0xD10103FF, 0xA9017BFD},
    {0xD45810, 0xA9BD7BFD, 0xF9000BF5}, {0xD7D620, 0x34000262, 0xF940D008},
    {0x10CD5BC, 0xA9BD7BFD, 0xF9000BF5}, {0xB20A48, 0xA9BF7BFD, 0x910003FD},
    {0xB51B08, 0xA9BF7BFD, 0x910003FD}, {0xB964FC, 0xA9BF7BFD, 0x910003FD},
};
std::uintptr_t g_base{};
std::atomic<std::uintptr_t> g_manager{};
SavedState g_state;

template <class Fn> Fn call(std::uintptr_t offset) { return reinterpret_cast<Fn>(g_base + offset); }

struct Reader {
    std::uintptr_t manager;
    bool boolean(std::uint32_t hash, bool& value) const {
        std::uint8_t result = 0;
        const bool found = call<std::uint32_t(*)(std::uintptr_t, std::uint8_t*, std::uint32_t)>(
            0xD45810)(manager, &result, hash) & 1;
        value = result != 0;
        return found;
    }
    bool integer(std::uint32_t hash, std::int32_t& value) const {
        return call<GameDataGetIntFunction>(0x10CD5BC)(manager, &value, hash) & 1;
    }
    bool real(std::uint32_t hash, float& value) const {
        return call<std::uint32_t(*)(std::uintptr_t, float*, std::uint32_t)>(
            0xD7D620)(manager, &value, hash) & 1;
    }
};

struct Writer {
    std::uintptr_t manager;
    bool room(const std::array<int, 3>& pushes) const {
        constexpr std::array<std::uintptr_t, 3> stores{8, 200, 392};
        for (std::size_t i = 0; i < stores.size(); ++i)
            if (pushes[i] && !inspectGameDataQueue(manager + stores[i], pushes[i])) return false;
        return true;
    }
    void boolean(std::uint32_t hash, bool value) const {
        call<std::uint64_t(*)(std::uintptr_t, bool, std::uint32_t)>(0xB20A48)(manager, value, hash);
    }
    void integer(std::uint32_t hash, std::int32_t value) const {
        call<GameDataSetIntFunction>(0xB51B08)(manager, value, hash);
    }
    void real(std::uint32_t hash, float value) const {
        call<std::uint64_t(*)(std::uintptr_t, std::uint32_t, float)>(0xB964FC)(manager, hash, value);
    }
};

std::uintptr_t progressFile(std::uintptr_t manager) {
    if (!isPlausibleAddress(manager)) return 0;
    const auto count = readMemory<std::uint32_t>(manager + 0xCC0);
    const auto files = readMemory<std::uintptr_t>(manager + 0xCC8);
    return count && count <= 16 && isPlausibleAddress(files) ? files : 0;
}

bool loading(std::uintptr_t manager) {
    const auto file = progressFile(manager);
    return !file || (readMemory<std::uint8_t>(file + 0x78) & 2) != 0;
}

void restore(std::uintptr_t manager) {
    const auto snapshot = g_state.snapshot();
    if (snapshot.ready || loading(manager)) return;
    Reader reader{manager};
    States states{};
    const auto result = readStates(kSaveKeys, reader, states);
    if (result == StateTransfer::Ready && g_state.restore(snapshot.generation, states))
        Logging.Log("[champions] SAVE_RESTORED generation=%llu", snapshot.generation);
    else {
        static std::atomic<unsigned> reports{};
        if (reports.fetch_add(1) < 12)
            Logging.Log("[champions] SAVE_RESTORE_REFUSED result=%u generation=%llu",
                        unsigned(result), snapshot.generation);
    }
}

HOOK_DEFINE_TRAMPOLINE(SetSave) {
    static bool Callback(std::uintptr_t manager, std::uintptr_t buffer, std::uint32_t index) {
        if (index == 0) {
            const auto generation = g_state.invalidate();
            Logging.Log("[champions] SAVE_LOAD generation=%llu", generation);
        }
        return Orig(manager, buffer, index);
    }
};

HOOK_DEFINE_TRAMPOLINE(Calc) {
    static std::uintptr_t Callback(std::uintptr_t manager) {
        const auto previous = g_manager.exchange(manager, std::memory_order_acq_rel);
        if (previous != manager || (readMemory<std::uint32_t>(manager + 0xCB8) & 16))
            g_state.invalidate();
        const auto result = Orig(manager);
        restore(manager);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(Serialize) {
    static std::uintptr_t Callback(std::uintptr_t manager, std::uint32_t index) {
        const auto result = Orig(manager, index);
        if (index != 0 || manager != g_manager.load(std::memory_order_acquire)) return result;
        const auto file = progressFile(manager);
        if (!file) {
            Logging.Log("[champions] SAVE_PATCH_REFUSED reason=file-table");
            return result;
        }
        const auto buffer = readMemory<std::uintptr_t>(file + 0x60);
        const auto size = readMemory<std::uint32_t>(file + 0x58);
        const auto snapshot = g_state.snapshot();
        const auto patched = patchSaveBuffer(isPlausibleAddress(buffer)
            ? reinterpret_cast<unsigned char*>(buffer) : nullptr, size, kSaveKeys, snapshot);
        Logging.Log("[champions] SAVE_PATCH result=%u generation=%llu", unsigned(patched), snapshot.generation);
        return result;
    }
};
} // namespace

SavedState& state() { return g_state; }

StateTransfer flush(GameDataWriteReadiness readiness) {
    if (!g_base || readiness != GameDataWriteReadiness::LiveGameplayAndWorkerAlive) return StateTransfer::NotReady;
    const auto manager = resolveGameDataManagerFromHolder(g_base + 0x462E3D8);
    if (!manager || manager.value != g_manager.load(std::memory_order_acquire) || loading(manager.value))
        return StateTransfer::NotReady;
    const auto snapshot = g_state.snapshot();
    Reader reader{manager.value};
    States committed{};
    const auto read = readStates(kSaveKeys, reader, committed);
    if (read != StateTransfer::Ready) return read;
    Writer writer{manager.value};
    return writeStates(kSaveKeys, writer, committed, snapshot, true);
}

bool install(std::uintptr_t base) {
    if (!codeMatches(base, kSites, "champions", "GAMEDATA_BUILD_REFUSED")) return false;
    g_base = base;
    SetSave::InstallAtOffset(kSetSave);
    Calc::InstallAtOffset(kCalc);
    Serialize::InstallAtOffset(kSerialize);
    return true;
}
} // namespace botw_abilities::champions::data
