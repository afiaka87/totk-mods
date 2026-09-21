// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace arrowbound::pure {
using SettingsRecord = std::array<unsigned char, 24>;
inline constexpr std::size_t kSettingsFileBytes = 2 * sizeof(SettingsRecord);

inline std::uint32_t settingsChecksum(const SettingsRecord& record) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < 20; ++i) hash = (hash ^ record[i]) * 16777619u;
    return hash;
}

inline SettingsRecord settingsRecord(std::uint64_t sequence, bool enabled) {
    SettingsRecord record{'A', 'R', 'W', 'B', 'S', 'E', 'T', '1'};
    for (unsigned i = 0; i < 8; ++i) record[8 + i] = static_cast<unsigned char>(sequence >> (i * 8));
    record[16] = enabled ? 1 : 0;
    const auto hash = settingsChecksum(record);
    for (unsigned i = 0; i < 4; ++i) record[20 + i] = static_cast<unsigned char>(hash >> (i * 8));
    return record;
}

struct SettingsJournal {
    std::uint64_t sequence = 0;
    int slot = -1;
    bool enabled = false;

    void consider(const SettingsRecord& record, int index) {
        if (std::memcmp(record.data(), "ARWBSET1", 8) != 0 || record[16] > 1 ||
            record[17] || record[18] || record[19]) return;
        std::uint32_t hash = 0;
        for (unsigned i = 0; i < 4; ++i) hash |= std::uint32_t{record[20 + i]} << (i * 8);
        if (hash != settingsChecksum(record)) return;
        std::uint64_t seq = 0;
        for (unsigned i = 0; i < 8; ++i) seq |= std::uint64_t{record[8 + i]} << (i * 8);
        if (seq <= sequence) return;
        sequence = seq;
        slot = index;
        enabled = record[16] != 0;
    }

    bool next(bool desired, SettingsRecord& record, int& index) const {
        if (sequence == UINT64_MAX) return false;
        record = settingsRecord(sequence + 1, desired);
        index = slot == 0 ? 1 : 0;
        return true;
    }
};

class PersistentToggle {
public:
    bool enabled() const { return (request_.load(std::memory_order_acquire) & 1) != 0; }
    void request(bool enabled) {
        auto old = request_.load(std::memory_order_relaxed);
        const auto desired = [enabled](std::uint64_t state) { return ((state + 2) & ~1ull) | (enabled ? 1 : 0); };
        while (!request_.compare_exchange_weak(old, desired(old), std::memory_order_release,
                                               std::memory_order_relaxed)) {}
    }

    template <class Store>
    void service(Store& store) {
        if (!loaded_) {
            const std::uint64_t initial = store.load() ? 1 : 0;
            std::uint64_t expected = 0;
            if (request_.compare_exchange_strong(expected, initial, std::memory_order_acq_rel))
                serviced_ = initial;
            loaded_ = true;
        }
        const auto state = request_.load(std::memory_order_acquire);
        if (state == serviced_) return;
        store.save((state & 1) != 0);
        // A failed write stays session-only. A new user action can retry, never every frame.
        serviced_ = state;
    }

private:
    std::atomic<std::uint64_t> request_{0};
    std::uint64_t serviced_ = 0;
    bool loaded_ = false;
};
}
