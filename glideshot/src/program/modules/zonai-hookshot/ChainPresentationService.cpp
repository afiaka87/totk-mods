// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ChainPresentationService.hpp"

#include "ChainPresentation.hpp"
#include "ChainRenderer.hpp"
#include "ChainVisual.hpp"
#include "PositionZip.hpp"
#include "HookshotWorld.hpp"
#include "TargetingService.hpp"

namespace zonai_hookshot::presentation {
using namespace zonai_hookshot::pure;

void publishInvisible() { render::publish({}); }

void publishChain(HookshotRuntime& runtime) {
    if (!world::ready()) {
        publishInvisible();
        return;
    }
    ChainSnapshot snap{};
    const TargetSample* previewSample = &runtime.aim.sample;
    const float maxSpan = kMaximumSpan;
    Vec3 origin = targeting::chainOrigin();
    // The game draws Link one step behind the requested position during the zip, so the chain
    // starts from the position exposed at tick start.
    if (runtime.machine.phase == Phase::PositionCruise &&
        runtime.positionDrive.haveShown) {
        origin = runtime.positionDrive.shown;
        origin.y += targeting::kOriginUp;
    }
    switch (runtime.machine.phase) {
        case Phase::Targeting:
        case Phase::Confirming:
            snap = preview(origin, runtime.aim.sample, runtime.aim.verdict);
            break;
        case Phase::ChainLaunch:
            snap = launchSnapshot(origin, runtime.launch, maxSpan);
            break;
        case Phase::Latched:
        case Phase::PositionCruise:
        case Phase::Capture:
        case Phase::DetachRequest:
        case Phase::FallCruise:
        case Phase::GlideHandoff:
        case Phase::GlideTerminal:
            snap = latched(origin, runtime.launch.anchor, maxSpan);
            break;
        default: break;
    }

    render::Snapshot out{};
    out.visible = snap.visible ? 1u : 0u;
    Vec3 nearPoint = snap.nearPoint;
    Vec3 farPoint = snap.farPoint;
    Vec3 reticlePoint = snap.farPoint;
    if (snap.visible && (snap.style == ChainStyle::Launch ||
                         snap.style == ChainStyle::Latched)) {
        const float normalLength = length(runtime.aim.committedNormal);
        if (normalLength > 0.5f) {
            farPoint = add(farPoint, mul(runtime.aim.committedNormal,
                                         kAnchorVisualLift / normalLength));
            reticlePoint = add(runtime.launch.anchor,
                               mul(runtime.aim.committedNormal,
                                   kAnchorVisualLift / normalLength));
        }
    } else if (snap.visible && (snap.style == ChainStyle::PreviewValid ||
                                snap.style == ChainStyle::PreviewInvalid)) {
        const float normalLength = length(previewSample->normal);
        if (normalLength > 0.5f) {
            reticlePoint = add(reticlePoint,
                               mul(previewSample->normal,
                                   kAnchorVisualLift / normalLength));
        }
    }
    out.nearPoint[0] = nearPoint.x;
    out.nearPoint[1] = nearPoint.y;
    out.nearPoint[2] = nearPoint.z;
    out.farPoint[0] = farPoint.x;
    out.farPoint[1] = farPoint.y;
    out.farPoint[2] = farPoint.z;
    out.reticlePoint[0] = reticlePoint.x;
    out.reticlePoint[1] = reticlePoint.y;
    out.reticlePoint[2] = reticlePoint.z;
    out.style = static_cast<std::uint8_t>(snap.style);
    out.latchFeedbackTicks = runtime.aim.latchFeedbackTicks;

    render::publish(out);
}

}  // namespace zonai_hookshot::presentation
