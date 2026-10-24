// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <initializer_list>
#include "StasisRenderRoute.hpp"
#include "StasisModelView.hpp"
#include <array>
#include <vector>
using namespace stasis::render;

TEST_CASE("native Stasis draws world before bloom and HUD before mask release") {
    for (unsigned phase = 0; phase != 4; ++phase) {
        const auto route = nativeSchedule(phase, 0, 0);
        CHECK(route.afterWorld == (phase == 1));
        CHECK(route.beforeHud == (phase == 3));
        CHECK_FALSE((route.afterWorld && route.beforeHud));
        for (const auto ignored : {nativeSchedule(phase, 1, 0), nativeSchedule(phase, 0, 1)}) {
            CHECK_FALSE(ignored.afterWorld);
            CHECK_FALSE(ignored.beforeHud);
        }
    }
}

TEST_CASE("mask queues retain all model materials and exclude foreign units") {
    std::array<std::array<std::uintptr_t,3>,5> entries{{
        {{1,0,100}},{{2,0,200}},{{3,0,101}},{{4,0,100}},{{5,0,201}}}};
    std::uintptr_t nodes[5],output[5]{};
    std::uint64_t matches[stasis::kQueueNodeCapacity / 64]{};
    for(unsigned i=0;i<5;++i) nodes[i]=reinterpret_cast<std::uintptr_t>(entries[i].data());
    auto read=[]<class T>(std::uintptr_t p) { T v;std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return v; };
    output[0]=100;output[1]=101;
    CHECK(stasis::selectQueueNodes(nodes,5,output,2,5,matches,read)==3);
    CHECK(output[0]==nodes[0]);CHECK(output[1]==nodes[2]);CHECK(output[2]==nodes[3]);
    output[0]=100;output[1]=101;
    CHECK(stasis::selectQueueNodes(nodes,5,output,2,2,matches,read)==3); // Overflow refuses the pass.
    CHECK(output[0]==100);CHECK(output[1]==101); // No partial output on refusal.
    output[2]=200; // Neighbor joins amber preview; equipment stays out.
    CHECK(stasis::selectQueueNodes(nodes,5,output,3,5,matches,read)==4);
    CHECK(output[1]==nodes[1]);CHECK(output[3]==nodes[3]);
    CHECK(stasis::selectQueueNodes(nodes,5,output,0,5,matches,read)==0);
}

TEST_CASE("in-place queue filtering matches an independent oracle at full capacity") {
    constexpr unsigned capacity=16384;
    std::vector<std::uintptr_t> nodes(stasis::kQueueNodeCapacity),entries(capacity),expected;
    std::vector<std::uintptr_t> nodeUnits(nodes.size());
    std::uint64_t matches[stasis::kQueueNodeCapacity / 64]{};
    for (unsigned i=0;i<nodes.size();++i) nodes[i]=0x100000+32*i;
    const auto original=nodes;
    auto read=[&]<class T>(std::uintptr_t p) { return static_cast<T>(nodeUnits[(p-16-0x100000)/32]); };
    for (unsigned shift=0;shift<4;++shift) {
        expected.clear();
        for (unsigned i=0;i<capacity;++i) entries[i]=i+1;
        for (unsigned i=0;i<nodes.size();++i) {
            const bool included=i%4==shift;
            nodeUnits[i]=included ? i/4+1 : capacity+1;
            if (included) expected.push_back(nodes[i]);
        }
        REQUIRE(stasis::selectQueueNodes(nodes.data(),unsigned(nodes.size()),entries.data(),
                    capacity,capacity,matches,read)==capacity);
        CHECK(entries==expected);
        CHECK(nodes==original);
    }
    for (unsigned i=0;i<capacity;++i) entries[i]=i+1;
    nodeUnits[0]=1; // One extra material node exceeds output capacity.
    CHECK(stasis::selectQueueNodes(nodes.data(),unsigned(nodes.size()),entries.data(),
                capacity,capacity,matches,read)==capacity+1);
    CHECK(entries.front()==1);CHECK(entries.back()==capacity);
    CHECK(stasis::selectQueueNodes(nodes.data(),stasis::kQueueNodeCapacity+1,entries.data(),
                capacity,capacity,matches,read)==capacity+1);
}

TEST_CASE("mask queue disables cached all-model draws and restores the exact header") {
    std::array<std::byte,256> queue;
    for(unsigned i=0;i<queue.size();++i) queue[i]=std::byte(i);
    const auto original=queue;
    std::uintptr_t selected[3]{};
    auto get=[&]<class T>(unsigned offset) { T v;std::memcpy(&v,queue.data()+offset,sizeof(v));return v; };
    {
        stasis::ScopedQueueNodes view(queue.data(),selected,3);
        CHECK(get.template operator()<std::uintptr_t>(32)==reinterpret_cast<std::uintptr_t>(selected));
        CHECK(get.template operator()<unsigned>(44)==3);
        CHECK(get.template operator()<unsigned>(64)==0);
        CHECK(std::memcmp(queue.data()+72,original.data()+72,184)==0);
    }
    CHECK(queue==original);
}
