// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "CampLedgerCodec.hpp"

#include "../PersistencePolicy.hpp"

#include <cstring>

namespace bivouac::persistence {
namespace {

struct LedgerHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t siteStride;
    std::uint32_t count;
    std::uint32_t nextSequence;
    std::uint32_t payloadFnv;
    std::uint32_t reserved[3];
};

struct LedgerSite {
    float x;
    float y;
    float z;
    float normalX;
    float normalZ;
    std::uint32_t sequence;
    std::uint32_t flags;
    std::uint32_t packedMetadata;
};

static_assert(sizeof(LedgerHeader) == kLedgerHeaderSize);
static_assert(sizeof(LedgerSite) == kLedgerSiteSize);

constexpr std::uint32_t kActiveFlag = 1u;
constexpr std::uint32_t kWaterModeFlag = 2u;

bool overlapsEarlierSite(const LedgerDocument& document, const LedgerSite& candidate,
                         float separationMeters) {
    for (std::uint32_t index = 0; index < document.count; ++index) {
        const CampRecord& existing = document.sites[index];
        if (positionsOverlap(existing.x, existing.y, existing.z,
                             candidate.x, candidate.y, candidate.z,
                             separationMeters)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::uint32_t fnv1a32(const unsigned char* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619u;
    }
    return hash;
}

EncodeResult encodeLedger(const CampRecord* records, std::size_t recordCount,
                          std::uint32_t nextSequence, unsigned char* output,
                          std::size_t outputCapacity) {
    if (records == nullptr && recordCount != 0) {
        return {EncodeError::NullInput};
    }
    if (output == nullptr) {
        return {EncodeError::NullOutput};
    }
    if (recordCount > kMaximumSites) {
        return {EncodeError::TooManyRecords};
    }

    std::uint32_t activeCount = 0;
    for (std::size_t index = 0; index < recordCount; ++index) {
        if (records[index].active) {
            ++activeCount;
        }
    }

    const std::size_t encodedSize =
        kLedgerHeaderSize + static_cast<std::size_t>(activeCount) * kLedgerSiteSize;
    if (outputCapacity < encodedSize) {
        return {EncodeError::OutputTooSmall};
    }

    std::size_t outputOffset = kLedgerHeaderSize;
    for (std::size_t index = 0; index < recordCount; ++index) {
        const CampRecord& source = records[index];
        if (!source.active) {
            continue;
        }
        const LedgerSite encoded = {
            source.x,
            source.y,
            source.z,
            source.normalX,
            source.normalZ,
            source.sequence,
            kActiveFlag | (source.waterMode ? kWaterModeFlag : 0u),
            packSiteMeta(source.tier, source.floorGap, source.roofGap, source.stampSlot),
        };
        std::memcpy(output + outputOffset, &encoded, sizeof(encoded));
        outputOffset += sizeof(encoded);
    }

    const std::size_t payloadSize =
        static_cast<std::size_t>(activeCount) * kLedgerSiteSize;
    const LedgerHeader header = {
        kLedgerMagic,
        kLedgerVersion,
        static_cast<std::uint16_t>(kLedgerSiteSize),
        activeCount,
        nextSequence,
        fnv1a32(output + kLedgerHeaderSize, payloadSize),
        {},
    };
    std::memcpy(output, &header, sizeof(header));
    return {EncodeError::None, encodedSize, activeCount};
}

DecodeResult decodeLedger(const unsigned char* input, std::size_t inputSize,
                          DecodeOptions options) {
    if (input == nullptr) {
        return {DecodeError::NullInput};
    }
    if (inputSize < kLedgerHeaderSize) {
        return {DecodeError::TooSmall};
    }

    LedgerHeader header{};
    std::memcpy(&header, input, sizeof(header));
    if (header.magic != kLedgerMagic) {
        return {DecodeError::MagicMismatch};
    }
    if (header.version != kLedgerVersion) {
        return {DecodeError::VersionMismatch};
    }
    if (header.siteStride != kLedgerSiteSize) {
        return {DecodeError::StrideMismatch};
    }
    if (!payloadShapeMatches(static_cast<std::int64_t>(inputSize),
                             kLedgerHeaderSize, kLedgerSiteSize,
                             header.count, kMaximumSites)) {
        return {DecodeError::PayloadShapeMismatch};
    }

    const std::size_t payloadSize =
        static_cast<std::size_t>(header.count) * kLedgerSiteSize;
    if (fnv1a32(input + kLedgerHeaderSize, payloadSize) != header.payloadFnv) {
        return {DecodeError::ChecksumMismatch};
    }

    DecodeResult result{};
    result.document.nextSequence = header.nextSequence > 0 ? header.nextSequence : 1;
    std::size_t inputOffset = kLedgerHeaderSize;
    for (std::uint32_t index = 0; index < header.count; ++index) {
        LedgerSite encoded{};
        std::memcpy(&encoded, input + inputOffset, sizeof(encoded));
        inputOffset += sizeof(encoded);

        if ((encoded.flags & kActiveFlag) == 0) {
            continue;
        }
        if (overlapsEarlierSite(result.document, encoded,
                                options.duplicateSeparationMeters)) {
            result.document.needsRewrite = true;
            continue;
        }

        CampRecord& decoded = result.document.sites[result.document.count++];
        decoded.x = encoded.x;
        decoded.y = encoded.y;
        decoded.z = encoded.z;
        decoded.normalX = encoded.normalX;
        decoded.normalZ = encoded.normalZ;
        decoded.sequence = encoded.sequence;
        const PackedSiteMeta metadata =
            unpackSiteMeta(encoded.packedMetadata, options.maximumTier);
        decoded.tier = metadata.tier;
        decoded.floorGap = metadata.floorGap;
        decoded.roofGap = metadata.roofGap;
        decoded.stampSlot = metadata.stampSlot;
        decoded.waterMode = (encoded.flags & kWaterModeFlag) != 0;
        decoded.active = true;
    }
    return result;
}

const char* decodeErrorName(DecodeError error) {
    switch (error) {
    case DecodeError::None:
        return "none";
    case DecodeError::NullInput:
        return "null input";
    case DecodeError::TooSmall:
        return "too small";
    case DecodeError::MagicMismatch:
        return "magic mismatch";
    case DecodeError::VersionMismatch:
        return "version mismatch";
    case DecodeError::StrideMismatch:
        return "stride mismatch";
    case DecodeError::PayloadShapeMismatch:
        return "count/size mismatch";
    case DecodeError::ChecksumMismatch:
        return "checksum mismatch";
    }
    return "unknown";
}

} // namespace bivouac::persistence
