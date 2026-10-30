// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/StateCodec.hpp"
#include <cassert>
using namespace material_columns;
using namespace material_columns::storage;
int main() {
    Preferences<96> input;
    input.columns = true;
    input.stockPattern = 8;
    input.selected = {23, 250};
    input.count = 96;
    for (std::size_t i = 0; i < input.count; ++i) input.items[i] = {i + 1, i + 1001, static_cast<std::uint32_t>(i * 10)};
    const ProfileKey key{0x123456789abcdef0ull, 0x1020304050607080ull, 0x1234567};
    std::array<std::uint8_t, maximumBytes<96>> bytes{};
    const auto size = encode(input, key, 7, bytes.data(), bytes.size());
    assert(size == bytes.size() && size < 2048);
    Preferences<96> result;
    std::uint64_t sequence = 0;
    assert(decode(bytes.data(), size, key, result, sequence));
    assert(sequence == 7 && result.columns && result.stockPattern == 8 && result.selected.id == 23);
    for (std::size_t i = 0; i < input.count; ++i) {
        assert(result.items[i].group == input.items[i].group && result.items[i].item == input.items[i].item);
        assert(result.items[i].rank == input.items[i].rank);
    }
    // Every possible truncation and every single-byte corruption is rejected atomically.
    for (std::size_t i = 0; i < size; ++i) {
        assert(!decode(bytes.data(), i, key, result, sequence));
        bytes[i] ^= 1;
        assert(!decode(bytes.data(), size, key, result, sequence));
        assert(result.selected.id == 23 && sequence == 7);
        bytes[i] ^= 1;
    }
    assert(!decode(bytes.data(), size, {key.userLow + 1, key.userHigh, key.playData}, result, sequence));
    assert(!decode(bytes.data(), size, {key.userLow, key.userHigh, key.playData + 1}, result, sequence));
    auto malformed = bytes;
    write(malformed.data(), headerBytes + entryBytes, input.items[0].group, 8);
    write(malformed.data(), size - checksumBytes, checksum(malformed.data(), size - checksumBytes), checksumBytes);
    assert(!decode(malformed.data(), size, key, result, sequence));
    assert(!encode(input, {}, 1, bytes.data(), bytes.size()));
    assert(!encode(input, key, 0, bytes.data(), bytes.size()));
    assert(!encode(input, key, 1, bytes.data(), bytes.size() - 1));
    input.count = 0;
    input.stockPattern = 9;
    const auto emptySize = encode(input, key, 8, bytes.data(), bytes.size());
    assert(emptySize == 72 && decode(bytes.data(), emptySize, key, result, sequence));
    assert(sequence == 8 && result.count == 0 && result.stockPattern == 9);
    assert(newest(false, 99, false, 100) == -1);
    assert(newest(true, 7, false, 100) == 0); // incomplete newer file preserves prior state
    assert(newest(false, 99, true, 8) == 1);
    assert(newest(true, 7, true, 8) == 1 && newest(true, 9, true, 8) == 0);
}
