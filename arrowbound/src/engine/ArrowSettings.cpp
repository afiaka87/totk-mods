// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "ArrowSettings.hpp"

#include <lib.hpp>
#include <nn/fs.h>

#include "SettingsJournal.hpp"

namespace arrowbound::settings {
namespace {
constexpr const char* kMount = "arrowbound";
constexpr const char* kDirectory = "arrowbound:/arrowbound";
constexpr const char* kPath = "arrowbound:/arrowbound/settings.bin";

class Store {
public:
    bool writeFlightStatus(const char* data, std::size_t size) {
        if (!mounted_ || size != 1024) return false;
        constexpr const char* path="arrowbound:/arrowbound/clock-coexistence-status.txt";
        (void)nn::fs::CreateDirectory(kDirectory);
        nn::fs::FileHandle handle{};
        auto result=nn::fs::OpenFile(&handle,path,nn::fs::OpenMode_ReadWrite);
        if (result.IsFailure()) {
            result=nn::fs::CreateFile(path,size);
            if (result.IsSuccess()) result=nn::fs::OpenFile(&handle,path,nn::fs::OpenMode_ReadWrite);
        }
        if (result.IsFailure()) return false;
        long existing=0;
        result=nn::fs::GetFileSize(&existing,handle);
        if (result.IsSuccess() && existing==static_cast<long>(size))
            result=nn::fs::WriteFile(handle,0,data,size,nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        nn::fs::CloseFile(handle);
        return result.IsSuccess() && existing==static_cast<long>(size);
    }
    bool load() {
        if (!mount()) return false;
        nn::fs::FileHandle handle{};
        auto result = nn::fs::OpenFile(&handle, kPath, nn::fs::OpenMode_Read);
        if (result.IsFailure()) {
            Logging.Log("[arrowbound] SETTINGS_DEFAULT_OFF open=%08x", result.GetInnerValueForDebug());
            return false;
        }
        std::array<pure::SettingsRecord, 2> records{};
        long size = 0;
        result = nn::fs::GetFileSize(&size, handle);
        if (result.IsSuccess() && size == static_cast<long>(pure::kSettingsFileBytes)) {
            result = nn::fs::ReadFile(handle, 0, records.data(), sizeof(records));
            if (result.IsSuccess()) {
                journal_.consider(records[0], 0);
                journal_.consider(records[1], 1);
            }
        }
        nn::fs::CloseFile(handle);
        Logging.Log("[arrowbound] SETTINGS_LOAD valid=%u enabled=%u sequence=%llu size=%ld result=%08x",
            journal_.slot >= 0, journal_.enabled, journal_.sequence, size, result.GetInnerValueForDebug());
        return journal_.enabled;
    }

    void save(bool desired) {
        pure::SettingsRecord record{};
        int slot = 0;
        if (!journal_.next(desired, record, slot) || !mount()) {
            Logging.Log("[arrowbound] SETTINGS_SESSION_ONLY sequence/mount unavailable");
            return;
        }
        (void)nn::fs::CreateDirectory(kDirectory);
        nn::fs::FileHandle handle{};
        auto result = nn::fs::OpenFile(&handle, kPath, nn::fs::OpenMode_ReadWrite);
        if (result.IsFailure()) {
            result = nn::fs::CreateFile(kPath, pure::kSettingsFileBytes);
            if (result.IsSuccess()) result = nn::fs::OpenFile(&handle, kPath, nn::fs::OpenMode_ReadWrite);
        }
        if (result.IsFailure()) {
            Logging.Log("[arrowbound] SETTINGS_SESSION_ONLY open=%08x", result.GetInnerValueForDebug());
            return;
        }
        long size = 0;
        result = nn::fs::GetFileSize(&size, handle);
        if (result.IsFailure() || size != static_cast<long>(pure::kSettingsFileBytes)) {
            nn::fs::CloseFile(handle);
            Logging.Log("[arrowbound] SETTINGS_SESSION_ONLY size=%ld result=%08x", size, result.GetInnerValueForDebug());
            return;
        }
        const auto offset = static_cast<long>(slot * sizeof(record));
        result = nn::fs::WriteFile(handle, offset, record.data(), record.size(),
            nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        pure::SettingsRecord readback{};
        if (result.IsSuccess()) result = nn::fs::ReadFile(handle, offset, readback.data(), readback.size());
        nn::fs::CloseFile(handle);
        if (result.IsFailure() || readback != record) {
            Logging.Log("[arrowbound] SETTINGS_SESSION_ONLY write/readback=%08x", result.GetInnerValueForDebug());
            return;
        }
        journal_.consider(record, slot);
        Logging.Log("[arrowbound] SETTINGS_SAVED enabled=%u sequence=%llu slot=%d", desired, journal_.sequence, slot);
    }

private:
    bool mount() {
        if (mounted_) return true;
        const auto result = nn::fs::MountSdCard(kMount);
        mounted_ = result.IsSuccess();
        if (!mounted_) Logging.Log("[arrowbound] SETTINGS_MOUNT_FAILED result=%08x", result.GetInnerValueForDebug());
        return mounted_;
    }
    bool mounted_ = false;
    pure::SettingsJournal journal_{};
};

Store g_store;
pure::PersistentToggle g_toggle;
}

bool enabled() { return g_toggle.enabled(); }
void request(bool enabled) { g_toggle.request(enabled); }
void service() { g_toggle.service(g_store); }
bool writeFlightStatus(const char* data, std::size_t size) { return g_store.writeFlightStatus(data,size); }
}
