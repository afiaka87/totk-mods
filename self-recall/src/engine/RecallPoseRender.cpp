#include "RecallOffsets121.hpp"
#include "RecallPoseRender.hpp"
#include "RecallScenePalette.hpp"
#include "RecallMonochromeFilter.hpp"

#include <atomic>
#include <new>
#include <lib.hpp>
#include "RecallFrameHooks.hpp"
#include "RecallNativeRenderInput.hpp"
#include "RecallNativeShapeVisibility.hpp"
#include "RecallNativeAdmission.hpp"
#include "RecallEquipmentArchive.hpp"
#include "RecallEquipmentEffects.hpp"
#include "RecallPoseSession.hpp"
#include "RecallPoseStorage.hpp"
#include "RecallRenderFrames.hpp"

namespace self_recall::pose_render {
using namespace offsets121::pose_render;
namespace {

constexpr std::size_t kEmbeddedModel = 0x138;

std::uintptr_t g_mainBase = 0;
alignas(pure::RenderFrameStore) std::byte g_storage[sizeof(pure::RenderFrameStore)];
pure::RenderFrameStore* g_frames = nullptr;
std::atomic<std::uint64_t> g_failure{0};
std::atomic<std::uint64_t> g_uploads{0};
std::atomic<std::uint64_t> g_viewUploads{0};
std::atomic<std::uint64_t> g_bounds{0};
std::atomic<std::uint64_t> g_admissions{0};
std::atomic<std::uint64_t> g_startEpoch{0};
std::atomic<std::uint32_t> g_frameGeneration{0};
std::atomic<std::uint64_t> g_latchedEpoch{0};
std::atomic<std::uint64_t> g_admittedEpoch{0};
std::atomic<std::uint32_t> g_preparedGeneration{0};
std::atomic<std::uint64_t> g_suppressedEpoch{0};
std::atomic<std::uint64_t> g_originChanges{0}, g_culled{0}, g_wristPhaseDifferences{0};
std::array<std::atomic<std::uintptr_t>, pure::kPoseModelLimit> g_liveEquipment{};

static_assert(sizeof(pure::RenderFrameStore) + sizeof(model::CaptureWorkspace) +
              sizeof(pure::PoseHistory) + sizeof(pure::PoseHistorySlot) * pure::kHistoryCapacity <=
              pure::kPoseHistoryByteLimit + 3u * pure::kPoseBoneLimit * 12u * sizeof(float) + 64u * 1024u);

enum class Failure : unsigned { Collector = 1, Prepare, Session, Buffer, Model, Input,
                                Visibility, Admission, Upload };
void fail(Failure code, unsigned detail) {
    std::uint64_t expected = 0;
    const auto value = (static_cast<std::uint64_t>(code) << 32) | detail;
    (void)g_failure.compare_exchange_strong(expected, value, std::memory_order_release,
                                           std::memory_order_relaxed);
}

template <class T>
T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
    return value;
}

pure::RenderFrameStore::Lease acquire(const void* unit, unsigned buffer) {
    if (!g_frames || !pose_session::active()) return {};
    const auto generation = g_frameGeneration.load(std::memory_order_acquire);
    if (!generation) return {};
    auto found = g_frames->acquireEpoch(reinterpret_cast<std::uintptr_t>(unit),
        frame::diagnostics().frames, generation);
    if (!found.lease) {
        if (found.owned && !equipment::publishedModel(unit) &&
            frame::diagnostics().frames > g_startEpoch.load(std::memory_order_acquire))
            fail(Failure::Buffer, buffer);
    }
    return std::move(found.lease);
}

void logOriginChange(const pure::RenderFrameStore::Lease& frame,
                     const pure::RecordedModelPose& current, unsigned phase) {
    const auto count = g_originChanges.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count > 4 && count % 300 != 0) return;
    const auto& recorded = frame.get()->animation.models[frame.modelIndex()];
    Logging.Log("[self-recall] RENDER_ORIGIN_CHANGED count=%llu phase=%u model=%u epoch=%llu key=%llu "
                "relative=%u/%u origin=(%f,%f,%f)/(%f,%f,%f)",
        static_cast<unsigned long long>(count), phase, unsigned(frame.modelIndex()),
        static_cast<unsigned long long>(frame.get()->epoch),
        static_cast<unsigned long long>(frame.get()->animation.header.key.serial),
        unsigned(recorded.originRelative), unsigned(current.originRelative),
        double(recorded.renderOrigin[0]), double(recorded.renderOrigin[1]), double(recorded.renderOrigin[2]),
        double(current.renderOrigin[0]), double(current.renderOrigin[1]), double(current.renderOrigin[2]));
}

bool makeInput(const void* unit, const pure::RenderFrameStore::Lease& lease,
                model::NativeRenderInput& input) {
    model::View live;
    const auto described = model::describe(g_mainBase, unit, live);
    if (described != model::ViewStatus::Ready) {
        fail(Failure::Model, static_cast<unsigned>(described));
        return false;
    }
    const auto& recorded = lease.get()->animation;
    const auto prepared = lease.prepareModel(live.pose);
    if (prepared != pure::RenderModelStatus::Ready) {
        if (prepared == pure::RenderModelStatus::OriginChanged) logOriginChange(lease, live.pose, 1);
        fail(Failure::Prepare, 0x100u | static_cast<unsigned>(prepared));
        return false;
    }
    const auto& model = recorded.models[lease.modelIndex()];
    const auto status = input.prepare(live, model, recorded.bones + model.identity.firstBone);
    if (status != model::RenderInputStatus::Ready) {
        fail(Failure::Input, static_cast<unsigned>(status));
        return false;
    }
    return true;
}

void uploadHistoricalAnimation(void* unit) {
    palette::afterNativeModel(unit);
    const auto buffer = read<std::uint8_t>(unit, 0x15) & 3u;
    auto frame = acquire(unit, buffer);
    if (!frame) return;
    model::NativeRenderInput input;
    if (!makeInput(unit, frame, input)) return;
    using Calculate = void (*)(void*, unsigned);
    reinterpret_cast<Calculate>(g_mainBase + kCalculateSkeleton)(input.skeleton, buffer);
    reinterpret_cast<Calculate>(g_mainBase + kCalculateShape)(input.model, buffer);
    frame.markUploaded();
    monochrome::protectHistoricalModel(unit);
    const auto count = g_uploads.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == 1 || count % 1800 == 0)
        Logging.Log("[self-recall] animation upload: count=%llu epoch=%llu frame=%llu model=%u buffer=%u views=%llu",
            static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(frame.get()->epoch),
            static_cast<unsigned long long>(frame.get()->animation.header.key.serial),
            static_cast<unsigned>(frame.modelIndex()), buffer,
            static_cast<unsigned long long>(g_viewUploads.load(std::memory_order_relaxed)));
    return;
}

HOOK_DEFINE_TRAMPOLINE(BeforeDrawHook) {
    static std::uint64_t Callback(void* unit, void* context, const void* views, int viewCount) {
        const auto result = Orig(unit, context, views, viewCount);
        uploadHistoricalAnimation(unit);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(CalculateViewHook) {
    static void Callback(void* modelObject, unsigned view, const void* camera, unsigned buffer) {
        const auto* unit = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(modelObject) - kEmbeddedModel);
        auto frame = acquire(unit, buffer);
        model::NativeRenderInput input;
        if (frame && makeInput(unit, frame, input)) {
            Orig(input.model, view, camera, buffer);
            g_viewUploads.fetch_add(1, std::memory_order_relaxed);
        } else {
            Orig(modelObject, view, camera, buffer);
        }
    }
};

int recalledVisibility(const void* renderUnit) {
    const auto* unit = read<const void*>(renderUnit, 8);
    auto frame = acquire(unit, 0);
    if (!frame) {
        if (equipment::publishedModel(unit)) return 0;
        if (pose_session::active() && g_suppressedEpoch.load(std::memory_order_acquire) ==
                frame::diagnostics().frames) {
            for (const auto& live : g_liveEquipment)
                if (live.load(std::memory_order_relaxed) == reinterpret_cast<std::uintptr_t>(unit)) return 0;
        }
        return -1;
    }
    const auto visible = model::nativeShapeVisibility(unit,
        read<std::uint16_t>(renderUnit, 0x16), frame.get()->animation, frame.modelIndex());
    if (visible == pure::AnimationVisibility::Invalid) {
        fail(Failure::Visibility, frame.modelIndex());
        return 0;
    }
    return visible == pure::AnimationVisibility::Visible ? 1 : 0;
}
HOOK_DEFINE_TRAMPOLINE(ShapeIsVisibleHook) {
    static std::uint64_t Callback(const void* renderUnit) {
        const auto visible = recalledVisibility(renderUnit);
        return visible < 0 ? Orig(renderUnit) : static_cast<std::uint64_t>(visible);
    }
};

HOOK_DEFINE_TRAMPOLINE(ShapeDrawHook) {
    static std::uint64_t Callback(const void* unit, void* context, unsigned view, unsigned flags) {
        if (!drawVisible(unit)) return 0;
        return Orig(unit, context, view, flags);
    }
};
HOOK_DEFINE_TRAMPOLINE(ShapeArrayDrawHook) {
    static std::uint64_t Callback(const void* unit, void* context, unsigned view, unsigned flags) {
        if (!drawVisible(unit)) return 0;
        return Orig(unit, context, view, flags);
    }
};

using NativeCalculateBounding = float (*)(void*);
void updateHistoricalBounds(void* unit, NativeCalculateBounding calculate) {
    if (g_admittedEpoch.load(std::memory_order_acquire) != frame::diagnostics().frames) return;
    auto frame = acquire(unit, 0);
    if (!frame) return;
    const auto* history = pose_storage::history();
    auto source = history ? history->acquire(frame.get()->animation.header.key) : pure::PoseReadLease{};
    if (!source) { fail(Failure::Prepare, 0xB001); return; }
    const auto& recorded = *source.get();
    const auto& historical = recorded.models[frame.modelIndex()];
    model::View live;
    const auto described = model::describe(g_mainBase, unit, live);
    if (described != model::ViewStatus::Ready) { fail(Failure::Model, unsigned(described)); return; }
    live.pose.originRelative = historical.originRelative;
    std::memcpy(live.pose.renderOrigin, historical.renderOrigin, sizeof(historical.renderOrigin));
    model::NativeRenderInput animation;
    const auto input = animation.prepare(live, historical, recorded.bones + historical.identity.firstBone);
    if (input != model::RenderInputStatus::Ready) { fail(Failure::Input, unsigned(input)); return; }
    model::NativeBoundingInput bounds;
    bounds.prepareHistorical(unit, animation,
        pure::translatedBoundsSpace(historical, frame.get()->rootOffset));
    (void)calculate(bounds.unit);
    frame.markBounded();
    g_bounds.fetch_add(1, std::memory_order_relaxed);
    return;
}

HOOK_DEFINE_TRAMPOLINE(CalculateBoundingHook) {
    static float Callback(void* unit) {
        const auto result = Orig(unit);
        updateHistoricalBounds(unit, [](void* model) { return Orig(model); });
        return result;
    }
};

} // namespace

bool drawVisible(const void* renderUnit) {
    return renderUnit && recalledVisibility(renderUnit) != 0;
}

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    g_frames = ::new (static_cast<void*>(g_storage)) pure::RenderFrameStore;
    BeforeDrawHook::InstallAtOffset(kBeforeDraw);
    CalculateViewHook::InstallAtOffset(kCalculateView);
    ShapeIsVisibleHook::InstallAtOffset(kShapeIsVisible);
    CalculateBoundingHook::InstallAtOffset(kCalculateBounding);
    ShapeDrawHook::InstallAtOffset(kShapeDraw);
    ShapeArrayDrawHook::InstallAtOffset(kShapeArrayDraw);
}

void admitModels(void* scene, std::uint64_t epoch, std::span<const model::View> current,
                 std::span<const void* const> roots) {
    if (!g_frames || !pose_session::active() || epoch <= g_startEpoch.load(std::memory_order_acquire)) return;
    if (current.empty()) { fail(Failure::Admission, 0x100); return; }
    auto frame = acquire(reinterpret_cast<const void*>(current[0].identity.unit), 0);
    if (!frame || frame.get()->epoch != epoch || frame.get()->animation.header.modelCount != current.size()) {
        fail(Failure::Admission, 0x101); return;
    }
    const auto& animation = frame.get()->animation;
    for (unsigned i = 0; i < current.size(); ++i) {
        const auto& a = animation.models[i].identity;
        const auto& b = current[i].identity;
        if (a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
            a.boneCount != b.boneCount || a.materialCount != b.materialCount) {
            fail(Failure::Admission, 0x200u | i); return;
        }
    }
    const auto plan = model::planNativeAdmission(scene, roots,
        {animation.models, animation.header.modelCount});
    if (plan.status != model::AdmissionStatus::Ready) {
        fail(Failure::Admission, 0x300u | static_cast<unsigned>(plan.status)); return;
    }
    using Request = void (*)(const void*);
    for (unsigned i = 0; i < plan.count; ++i)
        reinterpret_cast<Request>(g_mainBase + kRequestDraw)(plan.request[i]);
    for (unsigned i = 0; i < current.size(); ++i) {
        if (!animation.models[i].queueAdmission) continue;
        auto* unit = reinterpret_cast<std::byte*>(current[i].identity.unit);
        auto flags = read<std::uint16_t>(unit, 0x12);
        flags |= 0x200u;
        std::memcpy(unit + 0x12, &flags, sizeof(flags));
    }
    g_admittedEpoch.store(epoch, std::memory_order_release);
    const auto count = g_admissions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == 1 || count % 1800 == 0)
        Logging.Log("[self-recall] animation admission: count=%llu epoch=%llu models=%u roots=%u requested=%u bounds=%llu",
            static_cast<unsigned long long>(count), static_cast<unsigned long long>(epoch),
            static_cast<unsigned>(current.size()), static_cast<unsigned>(roots.size()), plan.count,
            static_cast<unsigned long long>(g_bounds.load(std::memory_order_relaxed)));
}

void verifyComplete(std::uint64_t epoch, const pure::RecordedPoseFrame& recorded,
             std::span<const model::View> current) {
    if (!g_frames || !pose_session::active() || epoch <= g_startEpoch.load(std::memory_order_acquire)) return;
    if (current.empty() || current.size() > pure::kPoseModelLimit) {
        fail(Failure::Prepare, static_cast<unsigned>(pure::RenderPrepareStatus::ModelMismatch));
        return;
    }
    auto currentFrame = g_frames->acquireEpoch(current[0].identity.unit, epoch, recorded.header.key.generation);
    if (!currentFrame.lease || currentFrame.lease.get()->animation.header.modelCount != current.size()) {
        fail(Failure::Prepare, static_cast<unsigned>(pure::RenderPrepareStatus::ModelMismatch));
        return;
    }
    const auto& animation = currentFrame.lease.get()->animation;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto& a = animation.models[i].identity;
        const auto& b = current[i].identity;
        if (a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
            a.boneCount != b.boneCount || a.materialCount != b.materialCount) {
            fail(Failure::Prepare, static_cast<unsigned>(pure::RenderPrepareStatus::ModelMismatch));
            return;
        }
        const auto visible = model::nativeModelVisibility(reinterpret_cast<const void*>(b.unit), animation,
                                                         static_cast<unsigned>(i));
        if (visible == pure::AnimationVisibility::Invalid) {
            fail(Failure::Visibility, static_cast<unsigned>(i));
            return;
        }
        if (visible == pure::AnimationVisibility::Visible) {
            if (!current[i].pose.queueAdmission) {
                fail(Failure::Admission, static_cast<unsigned>(i));
                return;
            }
            if (!currentFrame.lease.uploaded(static_cast<unsigned>(i))) {
                const auto* unit = reinterpret_cast<const void*>(b.unit);
                if (currentFrame.lease.bounded(static_cast<unsigned>(i)) &&
                    read<std::uint32_t>(unit, 0xC) == 0) {
                    const auto count = g_culled.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (count <= 8 || count % 1800 == 0)
                        Logging.Log("[self-recall] HISTORICAL_MODEL_CULLED count=%llu model=%u body_models=%u "
                                    "archive=%u epoch=%llu key=%llu position=(%f,%f,%f)",
                            static_cast<unsigned long long>(count), unsigned(i), unsigned(animation.header.bodyModelCount),
                            unsigned(equipment::publishedModel(unit)), static_cast<unsigned long long>(epoch),
                            static_cast<unsigned long long>(animation.header.key.serial),
                            double(animation.header.route.pose.position.x), double(animation.header.route.pose.position.y),
                            double(animation.header.route.pose.position.z));
                    continue;
                }
                Logging.Log("[self-recall] ANIMATION_UPLOAD_MISSING model=%u archive=%u bounds_shapes=%u cull=%u flags=%u epoch=%llu",
                    static_cast<unsigned>(i), unsigned(equipment::publishedModel(unit)),
                    unsigned(read<std::uint16_t>(unit, 0x24)), read<std::uint32_t>(unit, 0xC),
                    unsigned(read<std::uint16_t>(unit, 0x12)), static_cast<unsigned long long>(epoch));
                fail(Failure::Upload, static_cast<unsigned>(i));
                return;
            }
        }
    }
    g_preparedGeneration.store(animation.header.key.generation, std::memory_order_release);
}

void beginFrame(std::uint64_t epoch) {
    g_latchedEpoch.store(0, std::memory_order_release);
    g_admittedEpoch.store(0, std::memory_order_release);
    g_suppressedEpoch.store(0, std::memory_order_release);
    if (!g_frames || !pose_session::active()) {
        equipment_effects::selectFrame(nullptr);
        return;
    }
    pure::PosePresentation presentation;
    auto selected = pose_session::acquirePresentation(&presentation);
    if (!selected) { if (pose_session::active()) fail(Failure::Session, 0); return; }
    const auto result = g_frames->begin(*selected.get(), epoch, presentation.offset);
    if (result != pure::RenderPrepareStatus::Ready) {
        fail(Failure::Prepare, static_cast<unsigned>(result));
        return;
    }
    g_frameGeneration.store(selected.get()->header.key.generation, std::memory_order_release);
    g_latchedEpoch.store(epoch, std::memory_order_release);
    if (!equipment::selectAppearance(*selected.get())) {
        fail(Failure::Prepare, 0xA001); return;
    }
    equipment_effects::selectFrame(selected.get());
}

bool copyBone(const void* unit, unsigned bone, float out[12]) {
    if (!g_frames || !unit || !pose_session::active()) return false;
    const auto epoch = g_latchedEpoch.load(std::memory_order_acquire);
    const auto generation = g_frameGeneration.load(std::memory_order_acquire);
    auto found = g_frames->acquireEpoch(reinterpret_cast<std::uintptr_t>(unit), epoch, generation);
    return found.lease && found.lease.copyWorldBone(bone, out) && pose_session::active() &&
        g_latchedEpoch.load(std::memory_order_acquire) == epoch;
}

pure::RenderFrameStore::Lease currentFrame(const void* body, std::uint64_t epoch) {
    if (!g_frames || !pose_session::active() || !body ||
        g_latchedEpoch.load(std::memory_order_acquire) != epoch) return {};
    auto found = g_frames->acquireEpoch(reinterpret_cast<std::uintptr_t>(body), epoch,
                                      g_frameGeneration.load(std::memory_order_acquire));
    return std::move(found.lease);
}

void suppressEquipment(std::uint64_t epoch, std::span<const model::View> live) {
    g_suppressedEpoch.store(0, std::memory_order_release);
    if (live.size() > g_liveEquipment.size()) { fail(Failure::Collector, 0xE001); return; }
    for (unsigned i = 0; i < g_liveEquipment.size(); ++i)
        g_liveEquipment[i].store(i < live.size() ? live[i].identity.unit : 0, std::memory_order_relaxed);
    g_suppressedEpoch.store(epoch, std::memory_order_release);
}

std::uint32_t latchedGeneration(std::uint64_t epoch) {
    return epoch && g_latchedEpoch.load(std::memory_order_acquire) == epoch
        ? g_frameGeneration.load(std::memory_order_acquire) : 0;
}

PaletteModel paletteModel(const void* unit, std::uint64_t epoch, std::uint32_t generation,
                          unsigned* failureDetail) {
    if (!g_frames || !unit || !epoch || !generation) return PaletteModel::Foreign;
    auto found = g_frames->acquireEpoch(reinterpret_cast<std::uintptr_t>(unit), epoch, generation);
    if (!found.owned) return PaletteModel::Foreign;
    const auto unavailable = [&](unsigned reason) {
        if (failureDetail) *failureDetail = reason;
        return PaletteModel::Unavailable;
    };
    if (!found.lease) return unavailable(1);
    if (!found.lease.uploaded(found.lease.modelIndex())) return unavailable(2);
    model::View live;
    const auto described = model::describe(g_mainBase, unit, live);
    if (described != model::ViewStatus::Ready) return unavailable(0x100u | unsigned(described));
    const auto prepared = found.lease.validateUploadedModel(live.pose);
    if (prepared != pure::RenderModelStatus::Ready) {
        return unavailable(0x200u | unsigned(prepared));
    }
    return PaletteModel::Ready;
}

bool copyHeader(std::uint64_t epoch, std::uint32_t generation, pure::PoseFrameHeader& out) {
    return g_frames && g_frames->copyHeader(epoch, generation, out);
}

void collectorFailed(unsigned reason, unsigned detail) {
    fail(Failure::Collector, (reason << 16) | (detail & 0xFFFF));
}
bool copyWrist(std::uint32_t historyGeneration, pure::RenderWristFrame& out) {
    if (!historyGeneration) return false;
    pure::PosePresentation presentation;
    auto selected = pose_session::acquirePresentation(&presentation);
    if (!selected) return false;
    const auto& header = selected.get()->header;
    if (header.key.generation != historyGeneration || !header.haveWrist) return false;
    for (float value : header.wristMatrix) if (!std::isfinite(value)) return false;
    out.key = header.key;
    out.epoch = frame::diagnostics().frames;
    std::memcpy(out.matrix, header.wristMatrix, sizeof(out.matrix));
    pure::shiftMatrix(out.matrix, presentation.offset);
    pure::RenderWristFrame body;
    if (g_frames && g_frames->copyWrist(out.epoch, historyGeneration, body) && body.key != out.key) {
        const auto count = g_wristPhaseDifferences.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 8 || count % 1800 == 0)
            Logging.Log("[self-recall] WRIST_BODY_PHASE count=%llu epoch=%llu wrist=%llu body=%llu",
                static_cast<unsigned long long>(count), static_cast<unsigned long long>(out.epoch),
                static_cast<unsigned long long>(out.key.serial), static_cast<unsigned long long>(body.key.serial));
    }
    return pose_session::active();
}
std::uint64_t takeFailure() { return g_failure.exchange(0, std::memory_order_acq_rel); }
void begin() {
    g_startEpoch.store(frame::diagnostics().frames, std::memory_order_release);
    g_frameGeneration.store(0, std::memory_order_release);
    g_preparedGeneration.store(0, std::memory_order_release);
}
bool ready(std::uint32_t generation) {
    return generation && g_preparedGeneration.load(std::memory_order_acquire) == generation;
}
void reset() {
    const auto origins = g_originChanges.exchange(0, std::memory_order_relaxed);
    const auto culled = g_culled.exchange(0, std::memory_order_relaxed);
    const auto phases = g_wristPhaseDifferences.exchange(0, std::memory_order_relaxed);
    if (origins || culled || phases)
        Logging.Log("[self-recall] RENDER_PRESENTATION_SUMMARY origin_changes=%llu culled_models=%llu wrist_body_phase=%llu",
            static_cast<unsigned long long>(origins), static_cast<unsigned long long>(culled),
            static_cast<unsigned long long>(phases));
    g_failure.store(0, std::memory_order_release);
    g_frameGeneration.store(0, std::memory_order_release);
    g_preparedGeneration.store(0, std::memory_order_release);
}

} // namespace self_recall::pose_render
