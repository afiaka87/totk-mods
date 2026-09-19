// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstddef>
#include <cstdint>

namespace bivouac::persistence {

inline constexpr std::uint32_t kLedgerMagic = 0x31515642u; // "BVQ1"
inline constexpr std::uint16_t kLedgerVersion = 1;
inline constexpr std::size_t kLedgerHeaderSize = 32;
inline constexpr std::size_t kLedgerSiteSize = 32;
inline constexpr std::size_t kMaximumSites = 64;
inline constexpr std::size_t kMaximumLedgerBytes =
    kLedgerHeaderSize + kMaximumSites * kLedgerSiteSize;

struct CampRecord {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float normalX = 0.0f;
    float normalZ = 0.0f;
    std::uint32_t sequence = 0;
    std::uint8_t tier = 0;
    float floorGap = 0.0f;
    float roofGap = 0.0f;
    std::uint16_t stampSlot = 0xFFFFu;
    bool waterMode = false;
    bool active = false;
};

struct LedgerDocument {
    CampRecord sites[kMaximumSites] = {};
    std::uint32_t count = 0;
    std::uint32_t nextSequence = 1;
    bool needsRewrite = false;
};

enum class EncodeError : std::uint8_t {
    None = 0,
    NullInput,
    NullOutput,
    TooManyRecords,
    OutputTooSmall,
};

struct EncodeResult {
    EncodeError error = EncodeError::None;
    std::size_t bytesWritten = 0;
    std::uint32_t encodedCount = 0;

    constexpr explicit operator bool() const { return error == EncodeError::None; }
};

enum class DecodeError : std::uint8_t {
    None = 0,
    NullInput,
    TooSmall,
    MagicMismatch,
    VersionMismatch,
    StrideMismatch,
    PayloadShapeMismatch,
    ChecksumMismatch,
};

struct DecodeOptions {
    std::uint8_t maximumTier = 2;
    float duplicateSeparationMeters = 20.0f;
};

struct DecodeResult {
    DecodeError error = DecodeError::None;
    LedgerDocument document = {};

    constexpr explicit operator bool() const { return error == DecodeError::None; }
};

std::uint32_t fnv1a32(const unsigned char* data, std::size_t size);

EncodeResult encodeLedger(const CampRecord* records, std::size_t recordCount,
                          std::uint32_t nextSequence, unsigned char* output,
                          std::size_t outputCapacity);

DecodeResult decodeLedger(const unsigned char* input, std::size_t inputSize,
                          DecodeOptions options = {});

const char* decodeErrorName(DecodeError error);

} // namespace bivouac::persistence
