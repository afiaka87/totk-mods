// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>
#include <cstring>

namespace arrowbound::engine {

// setSlot @1A58EE0: check +8, icon animation state +16, custom icon pane +80.
inline constexpr std::uint32_t carrierIconState(bool enabled) { return enabled ? 1u : 4u; }
inline constexpr const char* kActiveCarrierIconActor = "NormalArrow";

class CarrierSlotAppearance {
public:
    CarrierSlotAppearance(void* options, bool carrier, bool enabled)
        : options_(carrier ? static_cast<unsigned char*>(options) : nullptr) {
        static_assert(sizeof(std::uintptr_t) == 8);
        if (!options_) return;
        std::memcpy(&actor_, options_, sizeof(actor_));
        check_ = options_[8];
        std::memcpy(&state_, options_ + 16, sizeof(state_));
        std::memcpy(&pane_, options_ + 80, sizeof(pane_));
        options_[8] = enabled ? 1 : 0;
        const auto state = carrierIconState(enabled);
        std::memcpy(options_ + 16, &state, sizeof(state));
        const std::uintptr_t noPane = 0;
        std::memcpy(options_ + 80, &noPane, sizeof(noPane));
        const auto iconActor = reinterpret_cast<std::uintptr_t>(kActiveCarrierIconActor);
        std::memcpy(options_, &iconActor, sizeof(iconActor));
    }

    ~CarrierSlotAppearance() {
        if (!options_) return;
        std::memcpy(options_, &actor_, sizeof(actor_));
        options_[8] = check_;
        std::memcpy(options_ + 16, &state_, sizeof(state_));
        std::memcpy(options_ + 80, &pane_, sizeof(pane_));
    }

    CarrierSlotAppearance(const CarrierSlotAppearance&) = delete;
    CarrierSlotAppearance& operator=(const CarrierSlotAppearance&) = delete;

private:
    unsigned char* options_;
    std::uintptr_t actor_ = 0;
    unsigned char check_ = 0;
    std::uint32_t state_ = 0;
    std::uintptr_t pane_ = 0;
};

}  // namespace arrowbound::engine
