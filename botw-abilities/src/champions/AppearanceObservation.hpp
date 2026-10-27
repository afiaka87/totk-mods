// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace botw_abilities::champions::appearance_observation {
template<class T, class Read>
bool fetch(Read read, std::uintptr_t address, T& value) {
    constexpr auto limit = std::uintptr_t{1} << 39;
    return address >= 0x1000 && address < limit - sizeof(T) && read(address, &value, sizeof(T));
}

template<class T, std::size_t N>
T field(const std::array<std::byte, N>& data, unsigned offset) {
    T value{};
    if (offset <= N && sizeof(T) <= N - offset)
        std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

struct AnimationObservation {
    bool matches{}, readable{};
    float frame{}, end{};
};

// AutoPlay owns one clip name and one skeletal record; the reader borrows them only during the callback.
template<class Read>
AnimationObservation inspectAnimation(Read read, std::uintptr_t autoplay, std::string_view expected) {
    AnimationObservation result;
    std::uintptr_t names{}, data{}, name{}, clips{}, record{};
    int count{};
    std::array<char, 32> text{};
    if (expected.empty() || expected.size() >= text.size()) return result;
    if (!fetch(read, autoplay, names) || !fetch(read, names, count) || count != 1 ||
        !fetch(read, names + 8, data) || !fetch(read, data, name) ||
        !read(name, text.data(), expected.size() + 1) || text[expected.size()] ||
        std::memcmp(text.data(), expected.data(), expected.size())) return result;
    result.matches = true;

    std::array<std::byte, 16> values{};
    if (!fetch(read, autoplay + 32, clips) || !fetch(read, clips, count) || count != 1 ||
        !fetch(read, clips + 8, record) || !fetch(read, record, values)) return result;
    const auto index = field<int>(values, 0);
    result.frame = field<float>(values, 4);
    result.end = field<float>(values, 8);
    result.readable = index >= 0 && std::isfinite(result.frame) &&
        std::isfinite(result.end) && result.end > 0;
    return result;
}

inline bool playbackComplete(float frame, float end) {
    return std::isfinite(frame) && std::isfinite(end) && end > 0 && frame >= end;
}
}
