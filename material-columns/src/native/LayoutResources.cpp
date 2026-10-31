// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "LayoutResources.hpp"
#include "GameVersion.hpp"
#include "../pure/ResourceRouting.hpp"
#include "LayoutAssets.hpp"
#include <lib.hpp>
#include <nn/fs.h>
#include <atomic>
#include <cstring>

namespace material_columns::native {
namespace {
constexpr std::uintptr_t lookupOffset = 0x22ab818;
constexpr std::uintptr_t animationOffset = 0x755494;
// TotK 1.2.1 FindResourceByName, verified against main121_named ARM instructions.
constexpr std::uint32_t lookupPrologue[] = {
    0xd10583ff, 0xa9117bfd, 0x910443fd, 0xf90093fc
};
constexpr std::uint32_t animationPrologue[] = {
    0xd10603ff, 0xa9127bfd, 0x910483fd, 0xa9136ffc
};
bool installed = false;

// Native layout construction can retain pointers and modify cached metadata. Keep
// one writable copy, loaded once from the game's existing content mount.
std::atomic<unsigned> assetState{0}; // empty, loading, ready, refused
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t crc = ~0u;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
bool loadAsset(const char* path, std::uint8_t* buffer, std::size_t bytes, std::uint32_t expected) {
    nn::fs::FileHandle file{};
    auto result = nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Read);
    if (result.IsFailure()) return false;
    s64 size{};
    result = nn::fs::GetFileSize(&size, file);
    bool good = result.IsSuccess() && size == static_cast<s64>(bytes);
    if (good) good = nn::fs::ReadFile(file, 0, buffer, bytes).IsSuccess();
    nn::fs::CloseFile(file);
    return good && bytes >= 20 && std::memcmp(buffer, "FLYT", 4) == 0 && crc32(buffer, bytes) == expected;
}
bool loadAssets() {
    unsigned expected = 0;
    if (assetState.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        const bool good =
            loadAsset("content:/MaterialColumns/ShortCut_00.bflyt", assets::parent, sizeof(assets::parent), assets::parentCrc) &&
            loadAsset("content:/MaterialColumns/MCColumns_00.bflyt", assets::columns, sizeof(assets::columns), assets::columnsCrc) &&
            loadAsset("content:/MaterialColumns/PaShortCutSortWindow_00.bflyt", assets::sort, sizeof(assets::sort), assets::sortCrc) &&
            loadAsset("content:/MaterialColumns/MCSlot_00.bflyt", assets::slot, sizeof(assets::slot), assets::slotCrc);
        assetState.store(good ? 2 : 3, std::memory_order_release);
        Logging.Log("[material-columns] RomFS layouts %s bytes=%u mount=content shared=1",
            good ? "ready" : "refused", unsigned(sizeof(assets::parent)+sizeof(assets::columns)+sizeof(assets::sort)+sizeof(assets::slot)));
        return good;
    }
    unsigned state;
    do { state = assetState.load(std::memory_order_acquire); } while (state == 1);
    return state == 2;
}

HOOK_DEFINE_TRAMPOLINE(ColumnLayoutLookup) {
    static const void* Callback(void* accessor, std::size_t* size,
                                std::uint32_t type, const char* name) {
        if (!name) return Orig(accessor, size, type, name);
        const void* data = nullptr;
        std::size_t bytes = 0;
        switch (resources::layoutFor(type, name)) {
        case resources::LayoutId::Parent:
            data = assets::parent; bytes = sizeof(assets::parent); break;
        case resources::LayoutId::Columns:
            data = assets::columns; bytes = sizeof(assets::columns); break;
        case resources::LayoutId::Sort:
            data = assets::sort; bytes = sizeof(assets::sort); break;
        case resources::LayoutId::Slot:
            data = assets::slot; bytes = sizeof(assets::slot); break;
        default: break;
        }
        if (data && loadAssets()) {
            if (size) *size = bytes;
            return data;
        }
        return Orig(accessor, size, type, name);
    }
};

HOOK_DEFINE_TRAMPOLINE(ColumnAnimationLookup) {
    static const void* Callback(void* accessor, const char* layout,
                                const char* tag, std::uint32_t* size) {
        // The borrowed-slot container has no animation targets of its own.
        // Its native child retains its existing animation controller and resources.
        if (layout && std::strcmp(layout, "MCSlot_00") == 0) {
            if (size) *size = 0;
            return nullptr;
        }
        return Orig(accessor, resources::animationLayout(layout), tag, size);
    }
};

bool checkGuard(std::uintptr_t base, std::uintptr_t targetOffset, const std::uint32_t (&expected)[4]) {
    const auto* actual = reinterpret_cast<const std::uint32_t*>(base + targetOffset);
    for (std::size_t i = 0; i < 4; ++i) {
        if (actual[i] != expected[i]) {
            Logging.Log("[material-columns] resource hook refused at +%x: got=%08x expected=%08x",
                        static_cast<unsigned>(targetOffset + i * 4), actual[i], expected[i]);
            return false;
        }
    }
    return true;
}
} // namespace

bool installLayoutResources(std::uintptr_t mainBase, bool validateOnly) {
    if (installed) return true;
    if (!mainBase) {
        Logging.Log("[material-columns] resource hook refused: main base=0");
        return false;
    }
    std::uint32_t lookup[4], animation[4];
    for (unsigned i=0; i<4; ++i) {
        lookup[i] = nativeWord(lookupOffset, i, lookupPrologue[i]);
        animation[i] = nativeWord(animationOffset, i, animationPrologue[i]);
    }
    if (!checkGuard(mainBase, nativeOffset(lookupOffset), lookup) ||
        !checkGuard(mainBase, nativeOffset(animationOffset), animation)) return false;
    if (validateOnly) return true;
    ColumnLayoutLookup::InstallAtOffset(nativeOffset(lookupOffset));
    ColumnAnimationLookup::InstallAtOffset(nativeOffset(animationOffset));
    installed = true;
    Logging.Log("[material-columns] RomFS layout routing installed (%u writable bytes; no private mount/cache)",
                static_cast<unsigned>(sizeof(assets::parent) + sizeof(assets::columns) + sizeof(assets::sort) + sizeof(assets::slot)));
    return true;
}
} // namespace material_columns::native
