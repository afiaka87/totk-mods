// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisAudio.hpp"
#include "CryonisEngine.hpp"
#include "Totk121Signatures.hpp"
#include <lib.hpp>
#include <atomic>
#include <cstring>
namespace cryonis::audio {
namespace {
using engine::call;
using engine::read;
using engine::valid;
bool g_ready{};
struct Handle {
    std::int8_t bank{};
    std::uint8_t padding{};
    std::int16_t index{-1};
    std::uint32_t serial{};
};
Handle g_preview;
bool g_previewRequested{};
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
constexpr const char *names[] = {
    "TotkCryonis_Sys_Item_IceMaker_Making", "TotkCryonis_IceMakerBlock_TouchMaking",
    "TotkCryonis_IceMakerBlock_TouchMaking_End", "TotkCryonis_IceMakerBlock_Crush",
    "TotkCryonis_UltraHand_Base_lp_00"};
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
}
void install(std::uintptr_t base) {
    if (!signatures::codeMatches(base, signatures::kAudio, "AUDIO_REFUSED"))
        return;
    AssetNameHook::InstallAtOffset(offsets::kEventCallbackSLinkReplaceAssetInfo);
    g_ready = true;
    Logging.Log("[cryonis] original DSP audio adapter installed");
}
void play(Cue cue) {
    const unsigned index = static_cast<unsigned>(cue);
    if (!g_ready || index >= 5)
        return;
    const auto instance = speaker();
    if (!instance) {
        Logging.Log("[cryonis] AUDIO_REFUSED no UI speaker cue=%u", index);
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
        if (!selected) {
            Logging.Log("[cryonis] AUDIO_REFUSED cache full cue=%u", index);
            return;
        }
        if (!call<bool (*)(std::uintptr_t, Locator *, const char *)>(
                offsets::kUserResourceSearchAssetCallTableByName)(resource, &source,
                                                                  "AmiiboMarker_OK") ||
            !source.table) {
            Logging.Log("[cryonis] AUDIO_REFUSED donor leaf cue=%u resource=%p", index,
                        reinterpret_cast<void *>(resource));
            return;
        }
        selected->table = *source.table;
        if ((selected->table.flags & 1) || !selected->table.parameters) {
            Logging.Log("[cryonis] AUDIO_REFUSED donor parameters cue=%u flags=%u", index,
                        unsigned(selected->table.flags));
            return;
        }
        selected->table.name = names[index];
        selected->table.hash = 2166136261u;
        for (const auto *p = names[index]; *p; ++p) {
            selected->table.hash ^= static_cast<unsigned char>(*p);
            selected->table.hash *= 16777619u;
        }
        // Existing SLink leaf supplies routing/parameters; the name hook selects our bank asset.
        selected->resource = resource;
        selected->cue = index;
        selected->published.store(true, std::memory_order_release);
    }
    Locator locator{};
    locator.table = &selected->table;
    Handle handle;
    call<std::uintptr_t (*)(std::uintptr_t, const Locator *, Handle *, void *)>(
        offsets::kUserInstanceEmitImpl)(instance, &locator, &handle, nullptr);
    if (cue == Cue::Preview)
        g_preview = handle;
    Logging.Log("[cryonis] AUDIO cue=%u handle=%d serial=%u", index, int(handle.index),
                handle.serial);
}
void preview(bool active) {
    if (active == g_previewRequested)
        return;
    g_previewRequested = active;
    if (active)
        play(Cue::Preview);
    else if (g_ready && g_preview.index >= 0) {
        // Native Handle::fade validates the generation before touching an event.
        call<void (*)(Handle *, unsigned)>(offsets::kHandleFade)(&g_preview, 6);
        g_preview = {};
        Logging.Log("[cryonis] AUDIO preview stopped");
    }
}
}
