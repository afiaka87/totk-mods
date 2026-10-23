// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace totk::engine::native_read {
inline bool admit(const void* base, std::size_t offset, std::size_t size) {
    const auto address = reinterpret_cast<std::uintptr_t>(base);
    constexpr auto limit = std::uintptr_t{1} << 39;
    return address >= 0x1000 && address < limit && offset < limit - address && size <= limit - address - offset;
}
template<class T> T read(const void* base, std::size_t offset) {
    T value{};
    if (admit(base, offset, sizeof(T))) std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(T));
    return value;
}
inline const void* at(const void* base, std::size_t offset) {
    return admit(base, offset, 1) ? static_cast<const std::byte*>(base) + offset : nullptr;
}
} // namespace totk::engine::native_read
