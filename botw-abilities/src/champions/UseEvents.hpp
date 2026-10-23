// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "State.hpp"
#include <atomic>

namespace botw_abilities::champions {
struct UseEvent {
    std::uint64_t generation = 0;
    std::uintptr_t scene = 0;
    Blessing blessing{};
    bool finalCharge = false;
};

template <std::size_t Capacity, class Event = UseEvent>
class UseEvents {
public:
    bool push(Event event) {
        Guard guard{lock_};
        if (count_ == Capacity) return false;
        events_[(head_ + count_) % Capacity] = event;
        ++count_;
        return true;
    }
    bool pop(std::uint64_t generation, std::uintptr_t scene, Event& event) {
        Guard guard{lock_};
        while (count_) {
            const auto next = events_[head_];
            head_ = (head_ + 1) % Capacity;
            --count_;
            if (next.generation != generation || next.scene != scene) continue;
            event = next;
            return true;
        }
        return false;
    }
    void clear() { Guard guard{lock_}; head_ = count_ = 0; }
private:
    static_assert(Capacity > 0);
    struct Guard {
        std::atomic_flag& lock;
        explicit Guard(std::atomic_flag& value) : lock(value) {
            while (lock.test_and_set(std::memory_order_acquire)) {}
        }
        ~Guard() { lock.clear(std::memory_order_release); }
    };
    std::atomic_flag lock_{};
    std::array<Event, Capacity> events_{};
    std::size_t head_ = 0, count_ = 0;
};
} // namespace botw_abilities::champions
