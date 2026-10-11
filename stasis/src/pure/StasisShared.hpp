// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <atomic>

namespace stasis {
// A value shared between game threads behind a spin flag; try* calls give up if it is held.
template <class T> class Shared {
  public:
    template <class F> bool tryUpdate(F change) {
        if (lock_.test_and_set(std::memory_order_acquire))
            return false;
        change(value_);
        lock_.clear(std::memory_order_release);
        return true;
    }
    template <class F> void update(F change) {
        while (!tryUpdate(change)) {
        }
    }
    bool tryRead(T &out) {
        return tryUpdate([&](const T &value) { out = value; });
    }
    bool tryWrite(const T &in) {
        return tryUpdate([&](T &value) { value = in; });
    }
    T read() {
        T out{};
        update([&](const T &value) { out = value; });
        return out;
    }
    void write(const T &in) {
        update([&](T &value) { value = in; });
    }

  private:
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    T value_{};
};
} // namespace stasis
