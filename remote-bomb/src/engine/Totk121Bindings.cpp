// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeBombActions.hpp"
#include <array>
#include <cstring>

namespace remote_bomb::engine {
namespace {
struct Guard {
    std::uintptr_t offset;
    std::array<std::uint32_t, 2> words;
};
// The first two instructions of each TotK 1.2.1 function the mod calls; any mismatch refuses binding.
constexpr Guard guards[] = {
    {0x11523B8, {0xF9411408, 0xB4000068}},
    {0x102A3C0, {0xF9411408, 0xB4000068}},
    {0x158B304, {0xD100C3FF, 0xA9017BFD}},
    {0x1593CA8, {0x37F803A1, 0xA9BE7BFD}},
    {0x6DBC20, {0xA9BE7BFD, 0xA9014FF4}},
    {0xF85448, {0x39402008, 0x35000088}},
    {0x83AA68, {0xD10143FF, 0xA9017BFD}},
    {0x9F9B28, {0xF9411408, 0xB40000E8}},
};
template <typename Fn>
Fn at(std::uintptr_t base, std::uintptr_t offset) { return reinterpret_cast<Fn>(base + offset); }
}
bool bindTotk121(std::uintptr_t mainBase, Bindings &out, BindingFailure *failure) {
    out = {};
    if (failure)
        *failure = {};
    const auto refuse = [&](BindingError error, std::uintptr_t offset,
                            std::uint64_t expected, std::uint64_t actual) {
        if (failure)
            *failure = {error, offset, expected, actual};
        return false;
    };
    if (!mainBase)
        return refuse(BindingError::NullImage, 0, 1, 0);
    for (const auto &guard : guards) {
        std::uint64_t actual{}, expected{};
        std::memcpy(&actual, reinterpret_cast<const void *>(mainBase + guard.offset), sizeof(actual));
        std::memcpy(&expected, guard.words.data(), sizeof(expected));
        if (actual != expected)
            return refuse(BindingError::SignatureMismatch, guard.offset, expected, actual);
    }
    out.getLifter = at<decltype(out.getLifter)>(mainBase, 0x11523B8);
    out.getChemical = at<decltype(out.getChemical)>(mainBase, 0x102A3C0);
    out.detonate = at<decltype(out.detonate)>(mainBase, 0x158B304);
    out.isExploded = at<decltype(out.isExploded)>(mainBase, 0x1593CA8);
    out.heldReference = at<decltype(out.heldReference)>(mainBase, 0x6DBC20);
    out.releaseReference = at<decltype(out.releaseReference)>(mainBase, 0xF85448);
    out.deleteActor = at<decltype(out.deleteActor)>(mainBase, 0x83AA68);
    out.requestDeath = at<decltype(out.requestDeath)>(mainBase, 0x9F9B28);
    return true;
}
}
