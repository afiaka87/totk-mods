// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include "GripHooks.hpp"
#include <vector>
using namespace zonai_hookshot::hooks;
namespace {
// Preserve the real relative locations/alignment, without accessing game data.
std::vector<std::uint64_t> memory((kGripController + 32) / 8);
ClimbCallback beforeManual{}, beforeRecall{};
bool captureActive{}, inputInstalled{};
std::vector<int> calls;
void* args[3]{};
std::uint64_t game(void* a, void* b, void* c) {
    CHECK_FALSE(captureActive);
    CHECK(a == args[0]); CHECK(b == args[1]); CHECK(c == args[2]);
    calls.push_back(0); return 0xfedcba9876543210;
}
std::uint64_t manual(void* a, void* b, void* c) {
    captureActive = false; calls.push_back(1);
    return beforeManual(a,b,c);
}
std::uint64_t recall(void* a, void* b, void* c) {
    const auto result = beforeRecall(a,b,c); calls.push_back(2); return result;
}
std::uintptr_t reset() {
    std::fill(memory.begin(), memory.end(), 0);
    const auto base = reinterpret_cast<std::uintptr_t>(memory.data());
    *reinterpret_cast<std::uint32_t*>(base+kGripController) = kGripControllerWord;
    *reinterpret_cast<std::uint32_t*>(base+kGripClimb) = kGripClimbWord;
    beforeManual=beforeRecall=nullptr;
    inputInstalled=false; captureActive=true; calls.clear();
    queryResult=0; queryPermission=Perm_Rx;
    exl::hook::patchCount=exl::hook::trampolineCount=0;
    exl::hook::original=reinterpret_cast<std::uintptr_t>(game);
    args[0]=&memory[0]; args[1]=&memory[1]; args[2]=&memory[2];
    return base;
}
void enableInput() { REQUIRE(beforeManual != nullptr); inputInstalled=true; }
}
TEST_CASE("Climb observer and paired input work with Recall installed in either order") {
    for(bool recallFirst : {false,true}) {
        const auto base=reset(); const auto site=base+kGripClimb;
        REQUIRE((reinterpret_cast<std::uintptr_t>(manual)&3)==0);
        REQUIRE((reinterpret_cast<std::uintptr_t>(recall)&3)==0);
        if(recallFirst) beforeRecall=exl::hook::Hook(site,recall,true);
        REQUIRE(installGripHooks(base,manual,beforeManual,enableInput));
        if(!recallFirst) beforeRecall=exl::hook::Hook(site,recall,true);
        REQUIRE(inputInstalled);
        // The installer is idempotent; there must still be only two patches.
        CHECK(installGripHooks(base,manual,beforeManual,enableInput));
        CHECK(exl::hook::patchCount==2);
        const auto entry=totk::render::decodePfxEntry(site,reinterpret_cast<const std::uint32_t*>(site));
        CHECK(reinterpret_cast<ClimbCallback>(entry.previous)(args[0],args[1],args[2])==0xfedcba9876543210);
        CHECK(calls==std::vector<int>{1,0,2});
        CHECK_FALSE(captureActive);
    }
}
TEST_CASE("Unsafe climb or input entries never enable either grip hook") {
    for(int failure=0; failure<6; ++failure) {
        const auto base=reset(); const auto site=base+kGripClimb;
        if(failure==0) *reinterpret_cast<std::uint32_t*>(base+kGripController)=0xd503201f;
        if(failure==1) *reinterpret_cast<std::uint32_t*>(site)=0xd503201f;
        if(failure>=2) {
            beforeRecall=exl::hook::Hook(site,failure==4 ? manual : recall,true);
            if(failure==2) queryPermission=1;
            if(failure==3) queryResult=1;
            if(failure==5) *reinterpret_cast<std::uint32_t*>(base+kGripController)=0x14000004;
        }
        const auto patches=exl::hook::patchCount;
        CHECK_FALSE(installGripHooks(base,manual,beforeManual,enableInput));
        CHECK_FALSE(inputInstalled); CHECK(beforeManual==nullptr);
        CHECK(exl::hook::patchCount==patches);
    }
}
