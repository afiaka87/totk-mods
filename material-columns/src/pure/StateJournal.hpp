// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StateCodec.hpp"
#include <cstring>

namespace material_columns::storage {
enum class FileRead { missing, present, error };
enum class LoadResult { loaded, missing, unavailable };

template<std::size_t Groups> class StateJournal {
public:
    template<class Files>
    LoadResult load(ProfileKey key, Preferences<Groups>& state, Files& files) {
        ready_ = false;
        if (!key.valid()) return LoadResult::unavailable;
        int best = -1;
        std::uint64_t bestSequence = 0;
        Preferences<Groups> bestState{};
        for (int slot = 0; slot < 2; ++slot) {
            std::size_t size = 0;
            const auto result = files.read(slot, readImage_.data(), readImage_.size(), size);
            if (result == FileRead::error) return LoadResult::unavailable;
            if (result == FileRead::missing) continue;
            Preferences<Groups> candidate{};
            std::uint64_t sequence = 0;
            if (decode(readImage_.data(), size, key, candidate, sequence) &&
                (best < 0 || sequence > bestSequence)) {
                best = slot;
                bestSequence = sequence;
                bestState = candidate;
            }
        }
        key_ = key;
        latest_ = best;
        sequence_ = bestSequence;
        ready_ = true;
        if (best < 0) return LoadResult::missing;
        state = bestState;
        return LoadResult::loaded;
    }
    template<class Files>
    bool save(const Preferences<Groups>& state, Files& files) {
        if (!ready_ || sequence_ == std::numeric_limits<std::uint64_t>::max()) return false;
        const auto size = encode(state, key_, sequence_ + 1, writeImage_.data(), writeImage_.size());
        if (!size) return false;
        const int target = latest_ == 0 ? 1 : 0;
        if (!files.write(target, writeImage_.data(), size)) return false;
        std::size_t readSize = 0;
        if (files.read(target, readImage_.data(), readImage_.size(), readSize) != FileRead::present ||
            readSize != size || std::memcmp(writeImage_.data(), readImage_.data(), size)) return false;
        latest_ = target;
        ++sequence_;
        return true;
    }
    bool ready() const { return ready_; }
    ProfileKey key() const { return key_; }
private:
    ProfileKey key_{};
    std::uint64_t sequence_{};
    int latest_ = -1;
    bool ready_{};
    std::array<std::uint8_t, maximumBytes<Groups>> readImage_{}, writeImage_{};
};
} // namespace material_columns::storage
