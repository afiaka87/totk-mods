// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstddef>
#include <cstdint>

namespace bivouac::persistence {

enum class StorageError : std::uint8_t {
    None = 0,
    Unavailable,
    CreateFailed,
    OpenFailed,
    ResizeFailed,
    ReadFailed,
    WriteFailed,
    FileTooLarge,
};

struct StorageResult {
    StorageError error = StorageError::None;
    std::uint32_t nativeResult = 0;

    constexpr explicit operator bool() const { return error == StorageError::None; }
};

struct ReadResult : StorageResult {
    std::int64_t bytesRead = 0;
};

class CampLedgerStore {
public:
    StorageResult mountSdCard() const;

    StorageResult write(const char* directory, const char* path,
                        const unsigned char* data, std::int64_t size) const;

    ReadResult read(const char* path, unsigned char* output,
                    std::int64_t maximumSize) const;
};

const char* storageErrorName(StorageError error);

} // namespace bivouac::persistence
