// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
// Plays the original Stasis sounds through a renamed copy of a UI sound entry.
#include "StasisAudio.hpp"
#include "StasisEngine.hpp"
#include "Totk121Signatures.hpp"
#include "totk/engine/ActorRoster.hpp"
#include <lib.hpp>
#include <atomic>
#include <cstring>
namespace stasis::audio {
namespace {
using engine::call, engine::read, engine::valid;
bool g_ready{};
struct Handle {
    std::int8_t bank{};
    std::uint8_t padding{};
    std::int16_t index{-1};
    std::uint32_t serial{};
};
struct Table {
    const char *name{};
    std::uint16_t assetId{}, flags{};
    std::int32_t duration{}, parent{};
    std::uint16_t enumIndex{};
    std::uint8_t solved{}, padding{};
    std::uint32_t hash{}, alignment{};
    std::uintptr_t parameters{}, condition{};
};
struct Locator {
    const Table *table{};
    std::uintptr_t auxiliary{};
    std::uint32_t type{}, padding{};
};
static_assert(sizeof(Table) == 48 && offsetof(Table, parameters) == 32 && sizeof(Handle) == 8);
struct Row {
    Table table{};
    std::uintptr_t resource{};
    unsigned cue{};
    std::atomic<bool> published{false};
};
Row g_rows[48];
constexpr const char *names[] = {"TotkStasis_Bitalock_Start_02",  "TotkStasis_Bitalock_Hit_06",
                                 "TotkStasis_Bitalock_Hitend_06", "TotkStasis_Bitalock_End_06",
                                 "TotkStasis_Bitalock_End_07",    "TotkStasis_Bitalock_End_08",
                                 "TotkStasis_BitaLock_Timer03"};
const Row *custom(const Table *table) {
    for (const auto &row : g_rows)
        if (row.published.load(std::memory_order_acquire) && &row.table == table)
            return &row;
    return nullptr;
}
HOOK_DEFINE_TRAMPOLINE(AssetNameHook) {
    static std::uintptr_t Callback(std::uintptr_t self, std::uintptr_t info,
                                   std::uintptr_t argument) {
        if (valid(argument))
            if (const auto *row = custom(read<const Table *>(argument + 0x20))) {
                const auto wrapper = read<std::uintptr_t>(argument);
                if (!valid(wrapper))
                    return 2;
                const auto *name = row->table.name;
                std::memcpy(reinterpret_cast<void *>(wrapper), &name, sizeof(name));
            }
        return Orig(self, info, argument);
    }
};
std::uintptr_t speaker() {
    const auto system = read<std::uintptr_t>(engine::moduleBase + offsets::kSoundSystem);
    if (!valid(system))
        return 0;
    const int count = read<int>(system + 32), offset = read<int>(system + 36);
    if (count < 0 || count > 256 || offset < 0 || offset > 1024)
        return 0;
    const auto anchor = system + 16;
    auto node = read<std::uintptr_t>(system + 24);
    for (int i = 0; i < count && node != anchor && valid(node); ++i) {
        if (node < static_cast<unsigned>(offset))
            return 0;
        const auto user = node - static_cast<unsigned>(offset),
                   name = read<std::uintptr_t>(user + 16);
        if (totk::engine::isPlausibleStringAddress(name) &&
            std::strcmp(reinterpret_cast<const char *>(name), "UI_GlobalSound") == 0) {
            const int instances = read<int>(user + 48), link = read<int>(user + 52);
            const auto head = read<std::uintptr_t>(user + 32);
            if (instances < 1 || instances > 1024 || link < 0 || link > 1024 ||
                head < static_cast<unsigned>(link))
                return 0;
            const auto instance = head - static_cast<unsigned>(link),
                       owner = read<std::uintptr_t>(instance + 0x58);
            return valid(instance) && valid(owner) && valid(read<std::uintptr_t>(owner + 0x18))
                       ? instance
                       : 0;
        }
        node = read<std::uintptr_t>(node + 8);
    }
    return 0;
}
} // namespace
void install() {
    if (!totk::engine::codeMatches(engine::moduleBase, signatures::kAudio, "stasis", "AUDIO_REFUSED"))
        return;
    AssetNameHook::InstallAtOffset(offsets::kEventCallbackSLinkReplaceAssetInfo);
    g_ready = true;
    Logging.Log("[stasis] original DSP audio adapter installed");
}
void play(Cue cue) {
    const unsigned index = static_cast<unsigned>(cue);
    if (!g_ready || index >= 7)
        return;
    const auto instance = speaker();
    if (!instance) {
        Logging.Log("[stasis] AUDIO_REFUSED no UI speaker cue=%u", index);
        return;
    }
    const auto owner = read<std::uintptr_t>(instance + 0x58),
               resource = read<std::uintptr_t>(owner + 0x18);
    Row *selected = nullptr;
    for (auto &row : g_rows)
        if (row.published.load(std::memory_order_acquire) && row.resource == resource &&
            row.cue == index) {
            selected = &row;
            break;
        }
    if (!selected) {
        for (auto &row : g_rows)
            if (!row.published.load(std::memory_order_acquire)) {
                selected = &row;
                break;
            }
        Locator source{};
        if (!selected ||
            !call<bool (*)(std::uintptr_t, Locator *, const char *)>(
                offsets::kUserResourceSearchAssetCallTableByName)(resource, &source,
                                                                  "AmiiboMarker_OK") ||
            !source.table)
            return;
        selected->table = *source.table;
        if ((selected->table.flags & 1) || !selected->table.parameters)
            return;
        selected->table.name = names[index];
        selected->table.hash = 2166136261u;
        for (const auto *p = names[index]; *p; ++p) {
            selected->table.hash ^= static_cast<unsigned char>(*p);
            selected->table.hash *= 16777619u;
        }
        // The copied entry keeps the original routing; the name hook selects the custom asset.
        selected->resource = resource;
        selected->cue = index;
        selected->published.store(true, std::memory_order_release);
    }
    Locator locator{};
    locator.table = &selected->table;
    Handle handle;
    call<std::uintptr_t (*)(std::uintptr_t, const Locator *, Handle *, void *)>(
        offsets::kUserInstanceEmitImpl)(instance, &locator, &handle, nullptr);
}
} // namespace stasis::audio
