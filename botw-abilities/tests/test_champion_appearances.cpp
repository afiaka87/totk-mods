// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "AppearanceState.hpp"
#include "AppearanceObservation.hpp"
#include <limits>

using namespace botw_abilities::champions;
namespace observe = appearance_observation;
using Action = AppearanceAction;

TEST_CASE("An activation waits for arrival then fades and deletes once") {
    AppearanceState state;
    CHECK(state.step(false,false,1) == Action::None);
    state.request();
    CHECK(state.step(false,false,1) == Action::Spawn);
    CHECK(state.step(false,false,4) == Action::None);
    CHECK(state.step(true,false,1) == Action::Arrived);
    CHECK(state.step(true,false,0.5f) == Action::Follow);
    CHECK(state.step(true,true,0.5f) == Action::Delete);
    CHECK(state.step(true,true,1) == Action::None);
    CHECK(state.step(false,true,1) == Action::Removed);
    CHECK(state.step(false,true,1) == Action::None);
}

TEST_CASE("A same-Champion retrigger queues one playback without duplicates") {
    AppearanceState state;
    state.request();
    CHECK(state.step(false,false,0) == Action::Spawn);
    state.request(); state.request();
    CHECK(state.step(true,false,0) == Action::Arrived);
    CHECK(state.step(true,true,0) == Action::Delete);
    CHECK(state.step(false,true,0) == Action::Removed);
    CHECK(state.step(false,false,0) == Action::Spawn);
    CHECK_FALSE(state.queued);
}

TEST_CASE("Four Champion activations have independent cleanup and scene reset") {
    std::array<AppearanceState,4> slots{};
    for (auto& slot : slots) { slot.request(); CHECK(slot.step(false,false,0) == Action::Spawn); }
    CHECK(slots[1].step(true,false,0) == Action::Arrived);
    CHECK(slots[1].step(true,true,0) == Action::Delete);
    CHECK(slots[0].step(false,false,0) == Action::None);
    slots = {};
    for (auto& slot : slots) CHECK(slot.step(false,false,0) == Action::None);
    CHECK(slots[1].step(true,false,0) == Action::Delete);
}

TEST_CASE("Missing actors and unreadable playback cannot spawn endlessly") {
    AppearanceState state;
    state.request();
    CHECK(state.step(false,false,0) == Action::Spawn);
    CHECK(state.step(false,false,6) == Action::Timeout);
    CHECK(state.step(false,false,6) == Action::None);
    CHECK(state.step(true,false,0) == Action::Delete);
    CHECK(state.step(false,false,0) == Action::Removed);
    state.request();
    CHECK(state.step(false,false,0) == Action::Spawn);
    CHECK(state.step(true,false,0) == Action::Arrived);
    CHECK(state.step(true,false,std::numeric_limits<float>::quiet_NaN()) == Action::Follow);
    CHECK(state.step(true,false,11) == Action::Delete);
    CHECK_FALSE(observe::playbackComplete(0,0));
    CHECK_FALSE(observe::playbackComplete(18,19));
    CHECK(observe::playbackComplete(19,19));
    CHECK_FALSE(observe::playbackComplete(std::numeric_limits<float>::infinity(),19));
}

namespace {
struct Memory {
    static constexpr std::uintptr_t base=0x10000;
    std::array<std::byte,0x1000> bytes{};
    template<class T> void put(std::uintptr_t offset,T value) {
        REQUIRE(offset+sizeof(T)<=bytes.size());
        std::memcpy(bytes.data()+offset,&value,sizeof(T));
    }
    bool read(std::uintptr_t address,void* output,std::size_t size) const {
        if (address<base || address-base>bytes.size() || size>bytes.size()-(address-base)) return false;
        std::memcpy(output,bytes.data()+address-base,size); return true;
    }
};
}

TEST_CASE("Native animation matching distinguishes all four heroes and validates frame records") {
    Memory memory;
    memory.put<std::uintptr_t>(0,Memory::base+0x100);
    memory.put<int>(0x100,1);
    memory.put<std::uintptr_t>(0x108,Memory::base+0x200);
    memory.put<std::uintptr_t>(0x200,Memory::base+0x300);
    memory.put<std::uintptr_t>(32,Memory::base+0x400);
    memory.put<int>(0x400,1);
    memory.put<std::uintptr_t>(0x408,Memory::base+0x500);
    memory.put<int>(0x500,0);
    memory.put<float>(0x504,19);
    memory.put<float>(0x508,19);
    const auto read=[&](auto address,void* output,auto size) { return memory.read(address,output,size); };
    for (const std::string_view name : {"Gift_Updraft","Gift_Healing","Gift_Impregnable","Gift_Thunderbolt"}) {
        std::array<char,32> text{};
        std::memcpy(text.data(),name.data(),name.size());
        memory.put(0x300,text);
        const auto result=observe::inspectAnimation(read,Memory::base,name);
        REQUIRE(result.readable);
        CHECK(observe::playbackComplete(result.frame,result.end));
        CHECK_FALSE(observe::inspectAnimation(read,Memory::base,"Gift").matches);
    }
    memory.put<float>(0x508,std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(observe::inspectAnimation(read,Memory::base,"Gift_Thunderbolt").readable);
    memory.put<float>(0x508,19);
    memory.put<int>(0x500,-1);
    CHECK_FALSE(observe::inspectAnimation(read,Memory::base,"Gift_Thunderbolt").readable);
    memory.put<int>(0x500,0);
    memory.put<int>(0x400,2);
    CHECK_FALSE(observe::inspectAnimation(read,Memory::base,"Gift_Thunderbolt").readable);
    memory.put<std::uintptr_t>(32,0);
    const auto missing = observe::inspectAnimation(read,Memory::base,"Gift_Thunderbolt");
    CHECK(missing.matches);
    CHECK_FALSE(missing.readable);
}
