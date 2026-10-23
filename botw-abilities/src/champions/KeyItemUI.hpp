// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "SavedState.hpp"
#include <cstring>

namespace botw_abilities::champions {

template <class Keys> int keyItemIndex(const Keys& keys, const char* actor) {
    if (!actor) return -1;
    for (std::size_t i = 0; i < kBlessingCount; ++i)
        if (std::strcmp(keys[i].actor, actor) == 0) return static_cast<int>(i);
    return -1;
}

struct ItemScan { bool complete = false; std::uint8_t found = 0; };

// A loading pouch also reads as empty, so trust emptiness only across several spaced live-gameplay scans.
struct EmptyPouch {
    static constexpr unsigned kScans = 3;
    unsigned scans = 0;
    bool confirmed(std::uint32_t count) {
        scans = count ? 0 : (scans < kScans ? scans + 1 : kScans);
        return scans >= kScans;
    }
};

template <class Keys, class ReadName>
ItemScan scanKeyItems(const Keys& keys, std::uint32_t count, ReadName read, bool emptyConfirmed = false) {
    if (!count && emptyConfirmed) return {true};
    if (!count || count > 200) return {};
    ItemScan result{true};
    for (std::uint32_t slot = 0; slot < count; ++slot) {
        const auto name = read(slot);
        if (!name) return {};
        const auto item = keyItemIndex(keys, name);
        if (item >= 0) result.found |= std::uint8_t(1u << item);
    }
    return result;
}

struct ItemSelection {
    int index = -1;
    std::uint64_t generation = 0;
    bool owns(int action) const { return index >= 0 && index < int(kBlessingCount) && (action == 15 || action == 16); }
};

class ItemAppearance {
public:
    ItemAppearance(void* options, bool active) : bytes_(static_cast<unsigned char*>(options)) {
        check_ = bytes_[8];
        std::memcpy(&state_, bytes_ + 16, 4);
        bytes_[8] = active ? 1 : 0;
        const std::uint32_t state = active ? 1 : 4;
        std::memcpy(bytes_ + 16, &state, 4);
    }
    ~ItemAppearance() {
        bytes_[8] = check_;
        std::memcpy(bytes_ + 16, &state_, 4);
    }
    ItemAppearance(const ItemAppearance&) = delete;
    ItemAppearance& operator=(const ItemAppearance&) = delete;
private:
    unsigned char* bytes_;
    unsigned char check_{};
    std::uint32_t state_{};
};

} // namespace botw_abilities::champions
