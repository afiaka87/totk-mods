// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "PreferenceStorage.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <nn/fs.h>
#include <cstdio>
#include <lib.hpp>

namespace material_columns::native {
namespace {
std::uintptr_t base{};
bool initialized{}, mounted{}, directoryReady{}, warned{};
storage::StateJournal<maximumColumns> journal;
storage::ProfileKey currentKey{};
constexpr const char* directory = "mcstate:/material-columns";
// FS result module2, descriptions1/2 (SDK fs_results.hpp).
constexpr std::uint32_t pathNotFound = 0x202, pathAlreadyExists = 0x402;

template<class T> T field(std::uintptr_t object, std::size_t offset) {
    return *reinterpret_cast<const T*>(object + offset);
}
bool profile(storage::ProfileKey& key) {
    const auto account = field<std::uintptr_t>(base, 0x4728620); // Actual BSS instance pointer.
    const auto dataSlot = field<std::uintptr_t>(base, 0x462e3d8);
    const auto data = dataSlot ? field<std::uintptr_t>(dataSlot, 0) : 0;
    if (!account || !data) return false;
    struct Uid { std::uint64_t low{}, high{}; } uid;
    using GetUser = bool (*)(std::uintptr_t, Uid*);
    using GetUInt = unsigned (*)(std::uintptr_t, std::uint32_t*, std::uint32_t);
    if (!reinterpret_cast<GetUser>(base + 0xe0804c)(account, &uid) ||
        !(reinterpret_cast<GetUInt>(base + 0x1068290)(data, &key.playData, 0xe3c3cc6e) & 1)) return false;
    key.userLow = uid.low;
    key.userHigh = uid.high;
    return key.valid();
}
void report(const char* operation, std::uint32_t result) {
    if (!warned) Logging.Log("[material-columns] preference storage %s failed result=%08x", operation, result);
    warned = true;
}
bool mount() {
    if (!mounted) {
        const auto result = nn::fs::MountSdCard("mcstate");
        if (result.IsFailure()) { report("mount", result.GetInnerValueForDebug()); return false; }
        mounted = true;
    }
    if (!directoryReady) {
        const auto result = nn::fs::CreateDirectory(directory);
        if (result.IsFailure() && result.GetInnerValueForDebug() != pathAlreadyExists) {
            report("directory", result.GetInnerValueForDebug()); return false;
        }
        directoryReady = true;
    }
    return true;
}
struct Files {
    storage::ProfileKey key;
    void path(char (&out)[128], int slot) const {
        std::snprintf(out, sizeof(out), "%s/%016llx%016llx-%08x-%c.bin", directory,
            static_cast<unsigned long long>(key.userHigh), static_cast<unsigned long long>(key.userLow),
            key.playData, slot == 0 ? 'a' : 'b');
    }
    storage::FileRead read(int slot, std::uint8_t* buffer, std::size_t capacity, std::size_t& count) {
        count = 0;
        char name[128]; path(name, slot);
        nn::fs::FileHandle file;
        auto result = nn::fs::OpenFile(&file, name, nn::fs::OpenMode_Read);
        if (result.IsFailure()) {
            if (result.GetInnerValueForDebug() == pathNotFound) return storage::FileRead::missing;
            report("open/read", result.GetInnerValueForDebug()); return storage::FileRead::error;
        }
        s64 size = 0;
        result = nn::fs::GetFileSize(&size, file);
        if (!result.IsFailure() && size >= 0 && static_cast<std::uint64_t>(size) <= capacity) {
            result = nn::fs::ReadFile(file, 0, buffer, static_cast<u64>(size));
            if (!result.IsFailure()) count = static_cast<std::size_t>(size);
        }
        nn::fs::CloseFile(file);
        if (result.IsFailure()) { report("read", result.GetInnerValueForDebug()); return storage::FileRead::error; }
        // An oversized/negative file is an invalid record, not an I/O failure. Codec sees size0.
        return storage::FileRead::present;
    }
    bool write(int slot, const std::uint8_t* buffer, std::size_t size) {
        char name[128]; path(name, slot);
        nn::fs::FileHandle file;
        auto result = nn::fs::OpenFile(&file, name, nn::fs::OpenMode_Write);
        if (result.IsFailure() && result.GetInnerValueForDebug() == pathNotFound) {
            result = nn::fs::CreateFile(name, static_cast<s64>(size));
            if (!result.IsFailure()) result = nn::fs::OpenFile(&file, name, nn::fs::OpenMode_Write);
        }
        if (result.IsFailure()) { report("open/write", result.GetInnerValueForDebug()); return false; }
        result = nn::fs::SetFileSize(file, static_cast<s64>(size));
        if (!result.IsFailure()) {
            const nn::fs::WriteOption flush{nn::fs::WriteOptionFlag_Flush};
            result = nn::fs::WriteFile(file, 0, buffer, static_cast<u64>(size), flush);
        }
        nn::fs::CloseFile(file);
        if (result.IsFailure()) { report("write/flush", result.GetInnerValueForDebug()); return false; }
        return true;
    }
};
} // namespace

bool initializePreferenceStorage(std::uintptr_t mainBase, bool validateOnly) {
    if (initialized) return true;
    if (!mainBase) { report("initialization/base", 0); return false; }
    constexpr totk::engine::CodeSite sites[]{
        {0xe0804c, 0xa9bf7bfd, 0x910003fd}, {0x1068290, 0x34000262, 0xf943d008}
    };
    if (!totk::engine::codeMatches(mainBase, sites, "material-columns", "PREFERENCE_API_REFUSED")) return false;
    if (validateOnly) return true;
    base = mainBase;
    initialized = true;
    return true;
}
PreferenceLoad loadPreferences(SavedPreferences& value) {
    storage::ProfileKey key;
    if (!initialized || !profile(key)) {
        currentKey = {};
        report("profile unavailable", 0);
        return {true, false};
    }
    const bool changed = !(key == currentKey);
    if (!changed && journal.ready() && journal.key() == key) return {false, true};
    currentKey = key;
    warned = false; // At most one detailed I/O diagnostic per menu-opening retry.
    if (!mount()) return {changed, false};
    Files files{key};
    const auto result = journal.load(key, value, files);
    return {changed, result != storage::LoadResult::unavailable};
}
bool savePreferences(const SavedPreferences& value) {
    if (!initialized || !currentKey.valid() || !journal.ready() || !(journal.key() == currentKey)) return false;
    Files files{currentKey};
    if (!journal.save(value, files)) { report("journal/read-back", 0); return false; }
    return true;
}
} // namespace material_columns::native
