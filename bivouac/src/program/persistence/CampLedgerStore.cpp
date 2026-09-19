// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "CampLedgerStore.hpp"

#include <lib.hpp>
#include <nn/fs.hpp>
#include "totk/engine/SdCardMount.hpp"

namespace nn::fs {
Result SetFileSize(FileHandle handle, s64 size);
} // namespace nn::fs

namespace bivouac::persistence {

StorageResult CampLedgerStore::mountSdCard() const {
    const auto result = totk::engine::ensureSdCardMounted();
    return result == totk::engine::SdCardMountResult::Failed
        ? StorageResult{StorageError::Unavailable, 0}
        : StorageResult{};
}

StorageResult CampLedgerStore::write(const char* directory, const char* path,
                                     const unsigned char* data,
                                     std::int64_t size) const {
    nn::fs::CreateDirectory(directory); // Already-existing is the normal case.

    nn::fs::FileHandle file;
    Result result = nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Write);
    if (R_FAILED(result)) {
        result = nn::fs::CreateFile(path, size);
        if (R_FAILED(result)) {
            return {StorageError::CreateFailed, result};
        }
        result = nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Write);
    }
    if (R_FAILED(result)) {
        return {StorageError::OpenFailed, result};
    }

    result = nn::fs::SetFileSize(file, size);
    if (R_FAILED(result)) {
        nn::fs::CloseFile(file);
        return {StorageError::ResizeFailed, result};
    }

    const nn::fs::WriteOption flush{nn::fs::WriteOptionFlag_Flush};
    result = nn::fs::WriteFile(file, 0, data, static_cast<u64>(size), flush);
    nn::fs::CloseFile(file);
    if (R_FAILED(result)) {
        return {StorageError::WriteFailed, result};
    }
    return {};
}

ReadResult CampLedgerStore::read(const char* path, unsigned char* output,
                                 std::int64_t maximumSize) const {
    nn::fs::FileHandle file;
    Result result = nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Read);
    if (R_FAILED(result)) {
        return {{StorageError::Unavailable, result}, -1};
    }

    s64 size = 0;
    result = nn::fs::GetFileSize(&size, file);
    if (R_FAILED(result)) {
        nn::fs::CloseFile(file);
        return {{StorageError::ReadFailed, result}, -1};
    }
    if (size < 0 || size > maximumSize) {
        nn::fs::CloseFile(file);
        return {{StorageError::FileTooLarge, 0}, -1};
    }

    result = nn::fs::ReadFile(file, 0, output, static_cast<u64>(size));
    nn::fs::CloseFile(file);
    if (R_FAILED(result)) {
        return {{StorageError::ReadFailed, result}, -1};
    }
    return {{}, size};
}

const char* storageErrorName(StorageError error) {
    switch (error) {
    case StorageError::None:
        return "none";
    case StorageError::Unavailable:
        return "unavailable";
    case StorageError::CreateFailed:
        return "create failed";
    case StorageError::OpenFailed:
        return "open failed";
    case StorageError::ResizeFailed:
        return "resize failed";
    case StorageError::ReadFailed:
        return "read failed";
    case StorageError::WriteFailed:
        return "write failed";
    case StorageError::FileTooLarge:
        return "file too large";
    }
    return "unknown";
}

} // namespace bivouac::persistence
