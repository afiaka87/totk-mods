// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/Totk121Offsets.hpp"
#include "totk/port/CodeCheck.hpp"
#include <span>

namespace botw_abilities::wolf {

// Zero when the owner or the pointer it holds is not a plausible address.
[[nodiscard]] inline std::uintptr_t pointerAt(std::uintptr_t owner, std::uintptr_t offset) {
    if (!totk::engine::isPlausibleAddress(owner)) return 0;
    const auto value = totk::engine::readMemory<std::uintptr_t>(owner + offset);
    return totk::engine::isPlausibleAddress(value) ? value : 0;
}

[[nodiscard]] inline std::uintptr_t actorComponent(std::uintptr_t actor, std::uintptr_t slot) {
    return pointerAt(pointerAt(actor, totk::engine::layout::kActorComponentRegistry), slot);
}

// One per adapter: holds the game's load address once that adapter's code checks pass.
// Game code and data are named by their 1.2.1 image offset; `at` and `singleton` give the running game's place.
class NativeApi {
public:
    // `only121` lists places 1.4.3 does not have; they are checked on 1.2.1 alone.
    [[nodiscard]] bool bind(std::uintptr_t base, std::span<const totk::port::Site> sites, const char* refusal,
                            std::span<const totk::port::Site> only121 = {}) {
        if (base_) return base_ == base;
        if (!totk::engine::isPlausibleAddress(base) ||
            !totk::port::codeMatches(base, sites, "wolf-link", refusal) ||
            (!totk::port::is143() && !totk::port::codeMatches(base, only121, "wolf-link", refusal))) return false;
        base_ = base;
        return true;
    }
    [[nodiscard]] bool ready() const { return base_ != 0; }
    [[nodiscard]] std::uintptr_t base() const { return base_; }
    template<class Fn> [[nodiscard]] Fn at(totk::port::Code offset) const { return reinterpret_cast<Fn>(base_ + offset); }
    // Engine singletons sit behind a static pointer to a pointer.
    [[nodiscard]] std::uintptr_t singleton(totk::port::Code slot) const {
        return ready() ? pointerAt(totk::engine::readMemory<std::uintptr_t>(base_ + slot), 0) : 0;
    }
private:
    std::uintptr_t base_{};
};

}
