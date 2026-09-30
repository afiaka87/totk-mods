// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace zonai_hookshot::pure::handheld {
// Player SLink's native glider equip, flap, rain and squeak asset keys share this prefix.
inline bool gliderSound(std::string_view key) {return key.starts_with("Parashawl");}
// Same identity/restoration contract as Self Recall's equipment effect masking.
struct EffectIdentity {
    std::uintptr_t executor{},emitter{};
    std::uint32_t id{};
    bool operator==(const EffectIdentity&) const = default;
};
template<unsigned Capacity> class EffectMasks {
    struct Entry {EffectIdentity identity{};std::uint32_t mask{};};
    std::array<Entry,Capacity> entries_{};
public:
    bool remember(EffectIdentity identity,std::uint32_t mask) {
        if(!identity.executor||!identity.emitter)return false;
        for(const auto& e:entries_)if(e.identity.executor==identity.executor)return false;
        for(auto& e:entries_)if(!e.identity.executor){e={identity,mask};return true;}
        return false;
    }
    std::optional<std::uint32_t> restore(EffectIdentity identity) {
        for(auto& e:entries_)if(identity.executor&&e.identity.executor==identity.executor) {
            const auto saved=e;e={};
            if(saved.identity==identity)return saved.mask;
            return {};
        }
        return {};
    }
};
}
