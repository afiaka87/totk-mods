// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "persistence/CampLedgerCodec.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int gFailures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                              \
            ++gFailures;                                                                            \
        }                                                                                           \
    } while (false)

using bivouac::persistence::CampRecord;
using bivouac::persistence::DecodeError;
using bivouac::persistence::EncodeError;

std::size_t encodeOneRecord(unsigned char* bytes, std::size_t capacity) {
    CampRecord record{};
    record.x = 1.0f;
    record.y = 2.0f;
    record.z = 3.0f;
    record.normalX = 0.0f;
    record.normalZ = 1.0f;
    record.sequence = 7;
    record.tier = 2;
    record.floorGap = 1.24f;
    record.roofGap = 2.50f;
    record.stampSlot = 7;
    record.waterMode = true;
    record.active = true;

    const auto encoded =
        bivouac::persistence::encodeLedger(&record, 1, 8, bytes, capacity);
    CHECK(encoded.error == EncodeError::None);
    CHECK(encoded.bytesWritten == 64);
    CHECK(encoded.encodedCount == 1);
    return encoded.bytesWritten;
}

void testGoldenRecordShape() {
    unsigned char bytes[bivouac::persistence::kMaximumLedgerBytes] = {};
    encodeOneRecord(bytes, sizeof(bytes));
    const unsigned char expectedHeader[] = {
        0x42, 0x56, 0x51, 0x31, 0x01, 0x00, 0x20, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x20, 0x4C, 0x34, 0xD7, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(std::memcmp(bytes, expectedHeader, sizeof(expectedHeader)) == 0);

    const unsigned char expectedRecord[] = {
        0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x80, 0x3F, 0x07, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x02, 0x3E, 0x7D, 0x08,
    };
    CHECK(std::memcmp(bytes + 32, expectedRecord,
                      sizeof(expectedRecord)) == 0);
}

void testRoundTripAndInactiveFiltering() {
    CampRecord records[3] = {};
    records[0] = {1.0f, 2.0f, 3.0f, 0.0f, 1.0f, 7, 2,
                  1.24f, 2.50f, 7, true, true};
    records[1] = {100.0f, 200.0f, 300.0f, 1.0f, 0.0f, 99, 1,
                  0.0f, 0.0f, 9, false, false};
    records[2] = {-50.0f, 8.0f, 110.0f, -1.0f, 0.0f, 12, 1,
                  0.20f, 0.40f, 0xFFFFu, false, true};

    unsigned char bytes[bivouac::persistence::kMaximumLedgerBytes] = {};
    const auto encoded =
        bivouac::persistence::encodeLedger(records, 3, 13, bytes, sizeof(bytes));
    CHECK(encoded.error == EncodeError::None);
    CHECK(encoded.encodedCount == 2);
    CHECK(encoded.bytesWritten == 96);

    const auto decoded =
        bivouac::persistence::decodeLedger(bytes, encoded.bytesWritten);
    CHECK(decoded.error == DecodeError::None);
    CHECK(decoded.document.count == 2);
    CHECK(decoded.document.nextSequence == 13);
    CHECK(!decoded.document.needsRewrite);
    CHECK(decoded.document.sites[0].waterMode);
    CHECK(decoded.document.sites[0].tier == 2);
    CHECK(decoded.document.sites[0].stampSlot == 7);
    CHECK(decoded.document.sites[1].sequence == 12);
    CHECK(!decoded.document.sites[1].waterMode);
}

void testDuplicateAndLegacyNormalization() {
    CampRecord records[2] = {};
    records[0] = {10.0f, 20.0f, 30.0f, 1.0f, 0.0f, 1, 0,
                  0.0f, 0.0f, 0xFFFFu, false, true};
    records[1] = {29.99f, 20.0f, 30.0f, 1.0f, 0.0f, 2, 9,
                  0.0f, 0.0f, 0xFFFFu, false, true};

    unsigned char bytes[bivouac::persistence::kMaximumLedgerBytes] = {};
    const auto encoded =
        bivouac::persistence::encodeLedger(records, 2, 0, bytes, sizeof(bytes));
    const auto decoded =
        bivouac::persistence::decodeLedger(bytes, encoded.bytesWritten);
    CHECK(decoded.error == DecodeError::None);
    CHECK(decoded.document.count == 1);
    CHECK(decoded.document.needsRewrite);
    CHECK(decoded.document.nextSequence == 1);
}

void testRefusals() {
    unsigned char bytes[bivouac::persistence::kMaximumLedgerBytes] = {};
    CHECK(bivouac::persistence::encodeLedger(nullptr, 1, 1, bytes, sizeof(bytes)).error
          == EncodeError::NullInput);
    CHECK(bivouac::persistence::encodeLedger(nullptr, 0, 1, nullptr, sizeof(bytes)).error
          == EncodeError::NullOutput);
    CampRecord tooMany[65] = {};
    CHECK(bivouac::persistence::encodeLedger(tooMany, 65, 1, bytes, sizeof(bytes)).error
          == EncodeError::TooManyRecords);

    unsigned char valid[bivouac::persistence::kMaximumLedgerBytes] = {};
    encodeOneRecord(valid, sizeof(valid));
    CHECK(bivouac::persistence::decodeLedger(nullptr, 64).error
          == DecodeError::NullInput);
    CHECK(bivouac::persistence::decodeLedger(valid, 31).error
          == DecodeError::TooSmall);

    unsigned char damaged[bivouac::persistence::kMaximumLedgerBytes] = {};
    std::memcpy(damaged, valid, sizeof(damaged));
    damaged[0] ^= 1;
    CHECK(bivouac::persistence::decodeLedger(damaged, 64).error
          == DecodeError::MagicMismatch);
    std::memcpy(damaged, valid, sizeof(damaged));
    damaged[4] = 2;
    CHECK(bivouac::persistence::decodeLedger(damaged, 64).error
          == DecodeError::VersionMismatch);
    std::memcpy(damaged, valid, sizeof(damaged));
    damaged[6] = 31;
    CHECK(bivouac::persistence::decodeLedger(damaged, 64).error
          == DecodeError::StrideMismatch);
    CHECK(bivouac::persistence::decodeLedger(valid, 63).error
          == DecodeError::PayloadShapeMismatch);
    std::memcpy(damaged, valid, sizeof(damaged));
    damaged[32] ^= 1;
    CHECK(bivouac::persistence::decodeLedger(damaged, 64).error
          == DecodeError::ChecksumMismatch);
}

} // namespace

int main() {
    testGoldenRecordShape();
    testRoundTripAndInactiveFiltering();
    testDuplicateAndLegacyNormalization();
    testRefusals();
    if (gFailures == 0) {
        std::puts("camp ledger codec tests: PASS");
        return 0;
    }
    std::printf("camp ledger codec tests: %d failure(s)\n", gFailures);
    return 1;
}
