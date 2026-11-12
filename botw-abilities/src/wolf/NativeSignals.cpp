// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeSignals.hpp"
#include "NativeApi.hpp"
#include <lib.hpp>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using totk::core::WorldPosition;
constexpr totk::port::Code kNoiseOwner = 0xFC12CC, kNoiseSend = 0x69987C, kLetterSend = 0x1F11380;
constexpr totk::port::Code kLookOwner = 0xFA82A0, kLookStart = 0x101EE28, kLookMove = 0x1320D58, kLookStop = 0x11B257C;
constexpr totk::port::Site kSites[]{
    {kNoiseOwner, 0xF9411408, 0xB4000068}, {kNoiseSend, 0xD10103FF, 0xA9027BFD},
    {kLetterSend, 0xA9BD7BFD, 0xF9000BF5}, {kLookOwner, 0xF9411408, 0xB4000068},
    {kLookStart, 0xA9BE7BFD, 0xA9014FF4}, {kLookMove, 0xA9BE7BFD, 0xA9014FF4},
    {kLookStop, 0x39457408, 0x360005A8},
};
constexpr totk::port::Code kEventSlot = 0x462E3E8;
constexpr std::uintptr_t kEventCurrent = 0x58, kLookEnabled = 349, kLetterBox = 888;
constexpr std::uint16_t kNoiseKind = 512;
constexpr float kHowlLevel = 10;
constexpr std::uint32_t kForceEscape = 1916731393, kNobody = UINT32_MAX;
NativeApi g_api;
bool g_noiseLogged{};

struct Noise {
    std::uint16_t kind = kNoiseKind;
    float level{};
    std::uint32_t target = kNobody;
    WorldPosition position{};
    std::uint8_t flags{};
    std::uint32_t reserved{};
};
static_assert(sizeof(Noise) == 32 && offsetof(Noise, position) == 12 && offsetof(Noise, flags) == 24);

struct Letter { std::uint32_t target{}, message{}; std::uint8_t reserved{}; };
struct Escape { std::uint32_t attacker = kNobody; WorldPosition position{}; };
static_assert(sizeof(Escape) == 16);

struct LookPoint { WorldPosition position{}; float yOffset{}; };
struct LookContext { std::uintptr_t reserved[2]{}; std::uintptr_t actor{}; };
struct LookEnd { std::uint32_t setting{}, activation = 1, priority = 2; };

std::uintptr_t lookOwner(std::uintptr_t actor) {
    const auto owner = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(kLookOwner)(actor);
    return isPlausibleAddress(owner) && (readMemory<std::uint8_t>(owner + kLookEnabled) & 1) ? owner : 0;
}
}

bool initializeSignalApi(std::uintptr_t base) { return g_api.bind(base, kSites, "SIGNAL_ABI_REFUSED"); }

bool mainEventPlaying() {
    const auto events = g_api.singleton(kEventSlot);
    return events && readMemory<std::uintptr_t>(events + kEventCurrent) != 0;
}

void emitNoise(const ActorReference& actor, const WorldPosition& position) {
    if (!actor.address()) return;
    const auto owner = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(kNoiseOwner)(actor.address());
    if (!isPlausibleAddress(owner)) {
        if (!g_noiseLogged) Logging.Log("[wolf-link] NOISE_REFUSED no-owner");
        g_noiseLogged = true;
        return;
    }
    const Noise noise{.level = kHowlLevel, .position = position};
    g_api.at<void(*)(std::uintptr_t, const Noise*)>(kNoiseSend)(owner + 32, &noise);
    if (!g_noiseLogged) Logging.Log("[wolf-link] NOISE_SENT kind=%u level=%.0f", unsigned(kNoiseKind), double(kHowlLevel));
    g_noiseLogged = true;
}

void sendFlee(const ActorReference& actor, std::uint32_t messageId, const WorldPosition& position) {
    if (!actor.address()) return;
    const Letter letter{messageId, kForceEscape};
    const Escape escape{.position = position};
    const auto sent = g_api.at<std::int32_t(*)(std::uintptr_t, std::uintptr_t, const Letter*, const Escape*)>(kLetterSend)(
        actor.address() + kLetterBox, messageId, &letter, &escape);
    Logging.Log("[wolf-link] FLEE id=%u sent=%u", messageId, unsigned(sent != -1));
}

void HeadLook::aim(const ActorReference& actor, const WorldPosition& point) {
    if (refused_ || !actor.address()) return;
    const auto owner = lookOwner(actor.address());
    if (!owner) {
        Logging.Log("[wolf-link] LOOK_REFUSED no-controller");
        refused_ = true;
        return;
    }
    if (!active_) {
        request_.position = point;
        Logging.Log("[wolf-link] LOOK_START");
        g_api.at<void(*)(std::uintptr_t, Request*)>(kLookStart)(owner, &request_);
        active_ = true;
        return;
    }
    const LookPoint look{point, 0};
    const LookContext context{.actor = actor.address()};
    g_api.at<void(*)(const LookPoint*, const LookContext*, Request*)>(kLookMove)(&look, &context, &request_);
}

void HeadLook::rest(const ActorReference& actor) {
    if (!active_) return;
    active_ = false;
    if (!actor.address()) return;
    if (const auto owner = lookOwner(actor.address())) {
        const LookEnd end;
        g_api.at<void(*)(std::uintptr_t, const LookEnd*)>(kLookStop)(owner, &end);
    }
}

}
