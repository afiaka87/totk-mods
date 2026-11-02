// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>

// Bounded LZ4 block decoding. Read supplies bytes from the verified padding chain; offsets can refer only to output already produced, never to a separate dictionary.
template<class Read>
bool decodeBlock(Read& read, std::uint8_t* output, std::size_t size) {
    std::size_t written = 0;
    const auto length = [&](std::size_t base, std::size_t& result) {
        result = base;
        if (base != 15) return true;
        std::uint8_t byte;
        do {
            if (!read(byte) || result > size || byte > size - result) return false;
            result += byte;
        } while (byte == 255);
        return true;
    };
    while (read.remaining()) {
        std::uint8_t token;
        if (!read(token)) return false;
        std::size_t count;
        if (!length(token >> 4, count) || count > size - written) return false;
        for (std::size_t i=0; i<count; ++i)
            if (!read(output[written++])) return false;
        if (!read.remaining()) return written == size;
        std::uint8_t low, high;
        if (!read(low) || !read(high)) return false;
        const std::size_t offset = low | (std::size_t(high) << 8);
        if (!offset || offset > written || !length(token & 15, count) ||
            count > size - written || size - written - count < 4) return false;
        count += 4;
        for (std::size_t i=0; i<count; ++i,++written) output[written] = output[written-offset];
    }
    return written == size;
}
