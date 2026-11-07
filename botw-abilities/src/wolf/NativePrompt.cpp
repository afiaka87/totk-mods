// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativePrompt.hpp"
#include "NativeApi.hpp"

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
NativeApi g_api;
constexpr CodeSite kSites[]{
    {0x6C2400, 0xD100C3FF, 0xA9017BFD}, {0xADE6A8, 0xF9411408, 0xB4000068},
    {0x6ECA90, 0xD101C3FF, 0xA9037BFD}, {0x652D10, 0xD10103FF, 0xA9027BFD},
};
// Mode 1 is the scripted prompt; Link's per-frame prompts use mode 0 and cannot replace it.
constexpr int kForcedPrompt = 1;
// These ids select the DoCommand_00 labels 0021 and 0020, which the game leaves unused.
constexpr int kStayHash = 400369613, kComeHash = -1448804070;


// Filled without taking a reference, so it needs no release.
struct LockLink {
    std::uintptr_t unusedVtable{}, data{};
    std::uint32_t id = UINT32_MAX;
    std::uint8_t enabled = 1, unused[3]{};
};
static_assert(sizeof(LockLink) == 0x18 && offsetof(LockLink, id) == 0x10 && offsetof(LockLink, enabled) == 0x14);
}

bool initializePromptApi(std::uintptr_t base) { return g_api.bind(base, kSites, "PROMPT_ABI_REFUSED"); }

bool showPrompt(PromptText text) {
    if (!g_api.ready() || text == PromptText::None) return false;
    const int hash = text == PromptText::Come ? kComeHash : kStayHash;
    return g_api.at<bool(*)(int, int)>(0x6ECA90)(hash, kForcedPrompt);
}

void hidePrompt() {
    if (g_api.ready()) static_cast<void>(g_api.at<bool(*)(int)>(0x652D10)(kForcedPrompt));
}

LockOn readLockOn(const ActorReference& player, std::uint32_t wolf) {
    if (!g_api.ready() || !player.address()) return LockOn::Nothing;
    const auto receiver = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0xADE6A8)(player.address());
    if (!isPlausibleAddress(receiver)) return LockOn::Nothing;
    LockLink link;
    if (!g_api.at<bool(*)(std::uintptr_t, LockLink*)>(0x6C2400)(receiver, &link)) return LockOn::Nothing;
    return link.id == wolf ? LockOn::Wolf : LockOn::Other;
}

}
