// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Raycast layout (TotK 1.2.1): hit flag byte +0x20, position +0x24, normal +0x30, distance +0x40, shape tag +0x50
// (bit 0 = NoClimb), body id +0x120, body pointer +0x170; motion type 1 keyframed, 2 dynamic, 0 static.

// Generation-and-sequence-tagged ray mailbox shared by the aim cast and the cruise probe; it classifies the hit body
// inside the physics worker while that body is alive.
#pragma once

#include "EngineNamespace.hpp"

#include <cstdint>

#include "TargetValidator.hpp"
#include "Vec3.hpp"
#include "RayQueryExclusion.hpp"
#include "totk/harness/Module.hpp"

namespace HOOKSHOT_ENGINE_NS::aim {
constexpr std::uint32_t kSolidMask = 0x20;
constexpr int kTimeoutTicks = 24;

// Identity of one request: world generation plus issue sequence.
struct RequestId {
    std::uint32_t sequence = 0;
    std::uint32_t generation = 0;
};

enum class Poll : std::uint8_t { Quiet, TimedOut, Ready };

void initialize(std::uintptr_t mainBase);

// False when busy; the sequence advances only when accepted so the confirmation floor stays exact;
// nearPoint is captured by value.
bool request(const pure::Vec3& from, const pure::Vec3& to,
             const pure::Vec3& nearPoint, const pure::Vec3& direction,
             std::uint32_t& issueSequence, std::uint32_t generation,
             std::uint64_t tick, const engine::RayGroup* exclude = nullptr);

Poll service(std::uint64_t tick, pure::TargetSample& out);

// Only an unclaimed request can be taken back: Pending -> Idle races safely against the worker's
// claim.
void abandonPending();

// Physics-thread callback from the RayCastWorker trampoline.
void observe(wwpg::RaycastFn original, const void* from, const void* object);

}  // namespace HOOKSHOT_ENGINE_NS::aim
