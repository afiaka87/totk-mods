// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include "totk/render/PfxHook.hpp"
#include <array>
#include <vector>

using namespace totk::render;
namespace {
constexpr std::uintptr_t site = 0x7100c30c48;
using Words = std::array<std::uint32_t, 5>;
Words pristine() { return {0xd104c3ff, 0xa90d7bfd, 0, 0, 0}; }
Words absolute(std::uintptr_t target) {
    return {0x58000051, 0xd61f0220, std::uint32_t(target), std::uint32_t(target >> 32), 0};
}
PfxCallback beforeSurvey{}, beforeGlideshot{};
std::vector<int> draws;
void* expectedExtension{};
void* expectedArgs{};
std::uint64_t game(void* extension, void* args) {
    CHECK(extension == expectedExtension);
    CHECK(args == expectedArgs);
    draws.push_back(0);
    return 0xabcdef1234567890;
}
std::uint64_t survey(void* extension, void* args) {
    const auto result = beforeSurvey(extension, args);
    draws.push_back(1);
    return result;
}
std::uint64_t glideshot(void* extension, void* args) {
    const auto result = beforeGlideshot(extension, args);
    draws.push_back(2);
    return result;
}
void reset() {
    beforeSurvey = beforeGlideshot = nullptr;
    draws.clear();
    queryResult = 0;
    queryPermission = Perm_Rx;
    exl::hook::trampolineCount = exl::hook::patchCount = 0;
    exl::hook::original = reinterpret_cast<std::uintptr_t>(&game);
}
}

TEST_CASE("PFX accepts exact vanilla and both exlaunch entry forms") {
    CHECK(decodePfxEntry(site, pristine().data()).kind == PfxEntryKind::Vanilla);
    for (const std::int64_t displacement : {4ll*5, 0x7fffffcll, -4ll, -0x8000000ll}) {
        auto words = pristine();
        words[0] = 0x14000000u | (std::uint32_t(displacement / 4) & 0x03ffffffu);
        const auto entry = decodePfxEntry(site, words.data());
        CHECK(entry.kind == PfxEntryKind::Branch);
        CHECK(entry.previous == site + displacement);
    }
    CHECK(decodePfxEntry(site, absolute(0x7200123450).data()).previous == 0x7200123450);
    CHECK(decodePfxEntry(site, absolute(0x7200123450).data()).kind == PfxEntryKind::Absolute);
}

TEST_CASE("PFX refuses unsupported instructions and unsafe destinations") {
    for (const auto first : {0xd503201fu, 0x94000010u, 0x54000010u, 0x58000050u}) {
        auto words = pristine(); words[0] = first;
        CHECK(decodePfxEntry(site, words.data()).kind == PfxEntryKind::Unsupported);
    }
    auto wrong = pristine(); wrong[1] ^= 1;
    CHECK(decodePfxEntry(site, wrong.data()).kind == PfxEntryKind::Unsupported);
    wrong = absolute(0x7200123450); wrong[1] ^= 1;
    CHECK(decodePfxEntry(site, wrong.data()).kind == PfxEntryKind::Unsupported);
    for (auto target : {std::uintptr_t(0), site, site+4, site+8, site+12, site+16, site+21})
        CHECK(decodePfxEntry(site, absolute(target).data()).kind == PfxEntryKind::Unsupported);
}

TEST_CASE("PFX installer preserves both callbacks and game exactly once in either order") {
    for (bool surveyFirst : {false, true}) {
        reset();
        alignas(8) auto words = pristine();
        const auto base = reinterpret_cast<std::uintptr_t>(words.data()) - kPfxHookOffset;
        auto first = surveyFirst ? survey : glideshot;
        auto second = surveyFirst ? glideshot : survey;
        REQUIRE((reinterpret_cast<std::uintptr_t>(first) & 3) == 0);
        REQUIRE((reinterpret_cast<std::uintptr_t>(second) & 3) == 0);
        auto& firstPrevious = surveyFirst ? beforeSurvey : beforeGlideshot;
        auto& secondPrevious = surveyFirst ? beforeGlideshot : beforeSurvey;
        REQUIRE(installPfxHook(base, first, firstPrevious, "first"));
        REQUIRE(installPfxHook(base, second, secondPrevious, "second"));
        CHECK(installPfxHook(base, second, secondPrevious, "idempotent"));
        CHECK(exl::hook::trampolineCount == 1);
        CHECK(exl::hook::patchCount == 2);
        expectedExtension = &words[0]; expectedArgs = &words[1];
        const auto top = decodePfxEntry(base + kPfxHookOffset, words.data()).previous;
        CHECK(reinterpret_cast<PfxCallback>(top)(expectedExtension, expectedArgs) == 0xabcdef1234567890);
        const std::vector<int> expected{0, surveyFirst ? 1 : 2, surveyFirst ? 2 : 1};
        CHECK(draws == expected);
    }
}

TEST_CASE("PFX installer refuses non-executable inaccessible and self targets without writes") {
    for (int failure = 0; failure < 4; ++failure) {
        reset();
        alignas(8) auto words = absolute(reinterpret_cast<std::uintptr_t>(failure == 2 ? survey : game));
        if (failure == 0) queryPermission = 1;
        if (failure == 1) queryResult = 1;
        if (failure == 3) words[0] = 0xd503201f;
        const auto before = words;
        const auto base = reinterpret_cast<std::uintptr_t>(words.data()) - kPfxHookOffset;
        CHECK_FALSE(installPfxHook(base, survey, beforeSurvey, "refuse"));
        CHECK(beforeSurvey == nullptr);
        CHECK(words == before);
        CHECK(exl::hook::patchCount == 0);
    }
}

TEST_CASE("PFX decodes the NOP-led absolute form only at entries that are 4 mod 8") {
    constexpr std::uintptr_t odd = site + 4;
    const std::uint32_t nopForm[5]{0xd503201f, 0x58000051, 0xd61f0220, 0x00123450, 0x72};
    CHECK(decodePfxEntry(odd, nopForm).kind == PfxEntryKind::Absolute);
    CHECK(decodePfxEntry(odd, nopForm).previous == 0x7200123450);
    CHECK(decodePfxEntry(site, nopForm).kind == PfxEntryKind::Unsupported);
    CHECK(decodePfxEntry(odd, absolute(0x7200123450).data()).kind == PfxEntryKind::Unsupported);
    const std::uint64_t target = odd + 16;
    const std::uint32_t inside[5]{0xd503201f, 0x58000051, 0xd61f0220, std::uint32_t(target & 0xffffffffu),
                                  std::uint32_t(target >> 32)};
    CHECK(decodePfxEntry(odd, inside).kind == PfxEntryKind::Unsupported);
}

TEST_CASE("PFX installer chains at a 4 mod 8 entry with another build's prologue") {
    constexpr PfxSite other{0x1234, 0xd10283ff, 0xa9017bfd};
    for (bool surveyFirst : {false, true}) {
        reset();
        alignas(8) std::array<std::uint32_t, 6> words{0, other.first, other.second, 0, 0, 0};
        const auto entry = reinterpret_cast<std::uintptr_t>(&words[1]);
        REQUIRE((entry & 7) == 4);
        const auto base = entry - other.offset;
        auto first = surveyFirst ? survey : glideshot;
        auto second = surveyFirst ? glideshot : survey;
        auto& firstPrevious = surveyFirst ? beforeSurvey : beforeGlideshot;
        auto& secondPrevious = surveyFirst ? beforeGlideshot : beforeSurvey;
        REQUIRE(installPfxHook(base, other, first, firstPrevious, "first"));
        CHECK(words[1] == 0xd503201f);
        REQUIRE(installPfxHook(base, other, second, secondPrevious, "second"));
        CHECK(exl::hook::trampolineCount == 1);
        expectedExtension = &words[0]; expectedArgs = &words[2];
        const auto top = decodePfxEntry(entry, &words[1], other.first, other.second).previous;
        CHECK(reinterpret_cast<PfxCallback>(top)(expectedExtension, expectedArgs) == 0xabcdef1234567890);
        const std::vector<int> expected{0, surveyFirst ? 1 : 2, surveyFirst ? 2 : 1};
        CHECK(draws == expected);
    }
}
