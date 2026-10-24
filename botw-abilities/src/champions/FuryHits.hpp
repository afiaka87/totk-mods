// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "FuryBalance.hpp"
#include <array>
#include <cstddef>

namespace botw_abilities::champions {
struct FuryTarget {
    std::uintptr_t address{};
    std::uint32_t id{};
    constexpr bool operator==(const FuryTarget&) const = default;
};
struct FuryHit {
    FuryTarget target{};
    FuryBalance balance{};
    std::uint64_t expires{};
    bool damagePending = false;
    bool reactionPending = false;
    constexpr bool pending() const { return damagePending || reactionPending; }
};

// The native adapter serializes access; this table owns each damage and reaction exactly once.
template<std::size_t Capacity>
class FuryHits {
public:
    bool insert(FuryTarget target, FuryBalance balance, std::uint64_t expires) {
        if (!target.address || !balance.damage) return false;
        for (const auto& hit : hits_)
            if (hit.pending() && hit.target == target) return false;
        for (auto& hit : hits_) {
            if (hit.pending()) continue;
            hit = {target, balance, expires, true, balance.reactionFrames != 0};
            return true;
        }
        return false;
    }
    std::optional<FuryBalance> takeDamage(FuryTarget target, bool ready = true) {
        if (!ready) return std::nullopt;
        for (auto& hit : hits_) {
            if (!hit.damagePending || hit.target != target) continue;
            hit.damagePending = false;
            return hit.balance;
        }
        return std::nullopt;
    }
    std::optional<FuryBalance> takeReaction(FuryTarget target, bool ready = true) {
        if (!ready) return std::nullopt;
        for (auto& hit : hits_) {
            if (!hit.reactionPending || hit.target != target) continue;
            hit.reactionPending = false;
            return hit.balance;
        }
        return std::nullopt;
    }
    std::size_t expire(std::uint64_t now) {
        std::size_t expired = 0;
        for (auto& hit : hits_) {
            if (!hit.pending() || now < hit.expires) continue;
            hit = {};
            ++expired;
        }
        return expired;
    }
    void clear() { hits_ = {}; }
    bool pending() const {
        for (const auto& hit : hits_)
            if (hit.pending()) return true;
        return false;
    }
private:
    std::array<FuryHit, Capacity> hits_{};
};

// TotK 1.2.1 InternalDamageCalculator::registerDamage consumes this 40-byte native packet.
struct FuryDamagePacket {
    std::int32_t damage{};
    std::uint32_t attacker[2]{};
    std::uint32_t reserved[2]{};
    std::uint32_t type = 9;
    std::uint32_t optionalValue{};
    std::uint32_t hasOptionalValue{};
    std::uint32_t mask = 0x400;
    std::uint8_t flags = 1;
    std::uint8_t padding[3]{};
};
static_assert(sizeof(FuryDamagePacket) == 40);
static_assert(offsetof(FuryDamagePacket, type) == 20);
static_assert(offsetof(FuryDamagePacket, mask) == 32);
static_assert(offsetof(FuryDamagePacket, flags) == 36);
} // namespace botw_abilities::champions
