#include "RecallCamera.hpp"
#include "RecallPoseSession.hpp"
#include "RecallPoseRecorder.hpp"
#include "RecallCameraFrame.hpp"
#include <atomic>
#include <cmath>
#include <lib.hpp>

namespace self_recall::camera {
namespace {
template <class T>
T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
    return value;
}

void alignResolvedCamera(void* state, float* camera) {
    if (!pose_session::active()) return;
    static std::atomic<unsigned> refusals{0}, frames{0};
    unsigned refusal = 0;
    pure::Pose shown;
    pure::CameraVerticalCorrection correction;
    if (!state || !camera) {
        refusal = 1;
    } else {
        const auto* component = static_cast<const std::byte*>(state) - 1096;
        const auto* target = read<const void*>(component, 3144);
        void* actor = target ? read<void*>(target, 24) : nullptr;
        if (!actor || !pose_recorder::presentationPose(actor, shown)) {
            refusal = 2;
        } else if (!pure::alignCameraHeight(camera, shown.position.y,
                       read<float>(state, 156), read<float>(state, 840), correction)) {
            refusal = 3;
        }
    }
    if (refusal) {
        const auto n = refusals.fetch_add(1) + 1;
        if (n <= 3 || n % 300 == 0)
            Logging.Log("[self-recall] CAMERA_FRAME_REFUSED reason=%u count=%u", refusal, n);
        return;
    }
    const auto n = frames.fetch_add(1) + 1;
    if (n <= 3 || n % 90 == 0)
        Logging.Log("[self-recall] CAMERA_FRAME count=%u root_delta=%.3f transition_delta=%.3f applied=%.3f shown_y=%.3f look_y=%.3f flags=%llx",
            n, correction.cachedRootDelta, correction.transitionDelta, correction.appliedDelta,
            shown.position.y, camera[4], static_cast<unsigned long long>(read<std::uint64_t>(state, 328)));
}

HOOK_DEFINE_TRAMPOLINE(ResolveCameraHook) {
    static void Callback(void* state, float* camera, float frameScale) {
        Orig(state, camera, frameScale);
        alignResolvedCamera(state, camera);
    }
};

HOOK_DEFINE_TRAMPOLINE(FollowTargetHook) {
    static void Callback(void* action, float* current, const float* target,
                         const float* cushion, bool predict, float amount, float delta) {
        Orig(action, current, target, cushion, predict, amount, delta);
        if (!pose_session::active() || !current || !target || !std::isfinite(target[1])) return;
        const auto error = target[1] - current[1];
        current[1] = target[1];
        static std::atomic<unsigned> corrections{0};
        if (std::abs(error) > 1) {
            const auto count = corrections.fetch_add(1) + 1;
            if (count <= 3 || count % 300 == 0)
                Logging.Log("[self-recall] CAMERA_VERTICAL_FOLLOW error=%.3f count=%u", error, count);
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(FinalTargetHook) {
    static float Callback(void* action, float* target, const void* direction) {
        const auto result = Orig(action, target, direction);
        if (!pose_session::active() || !action || !target || !std::isfinite(target[1])) return result;
        float previous;
        auto* height = static_cast<std::byte*>(action) + 0x98;
        std::memcpy(&previous, height, sizeof(previous));
        std::memcpy(height, target + 1, sizeof(previous));
        static std::atomic<unsigned> corrections{0};
        if (std::abs(target[1] - previous) > 1) {
            const auto count = corrections.fetch_add(1) + 1;
            if (count <= 3 || count % 300 == 0)
                Logging.Log("[self-recall] CAMERA_FINAL_VERTICAL error=%.3f count=%u", target[1] - previous, count);
        }
        return result;
    }
};
}
void install() {
    FollowTargetHook::InstallAtOffset(0x00AE59C8);
    FinalTargetHook::InstallAtOffset(0x00AE675C);
    ResolveCameraHook::InstallAtOffset(0x00A6DF60);
}
}
