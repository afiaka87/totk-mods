#include "totk/engine/ReadGuard.hpp"
#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallGraphicsEngine.hpp"
#include "RecallEffectsEngine.hpp"
#include "GameProfiles.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <new>
#include <optional>
#include <lib.hpp>
#include "FloatHook.hpp"
#include "RecallRender.hpp"

namespace self_recall::pose_render {
namespace {

struct PoseProfile {
    std::uintptr_t beforeDraw, calculateView, calculateSkeleton, calculateShape;
    std::uintptr_t shapeIsVisible, calculateBounding, requestDraw;
    std::uintptr_t shapeDraw, shapeArrayDraw;
    std::uintptr_t inlineBeforeDrawRejoin;
    std::size_t embeddedModel, bufferIndex;
};

struct InlineDrawProfile {
    std::array<std::uintptr_t, 4> sites;
    std::array<std::uint32_t, 4> words;
    std::array<unsigned, 4> unitRegisters;
    std::array<std::size_t, 4> unitOffsets;
};

const InlineDrawProfile& inlineDrawProfile() {
    static constexpr std::array<InlineDrawProfile, 9> builds{{
        {}, {}, {}, {}, {},
        {{0x17BC28, 0x17C6D8, 0x1DCE70, 0x1DD23C},
         {0xF9423D29, 0xF9432929, 0xF9423D29, 0x91210129}, {27, 27, 25, 25}, {}},
        {{0x164E58, 0x164FE0, 0x3A1800, 0},
         {0xF941F929, 0xF942E529, 0xEB09011F, 0}, {20, 20, 28, 0}, {0x10, 0x10, 0, 0}},
        {{0xD8FB4, 0xD9A3C, 0x2DD1E0, 0x2DD598},
         {0xF9423129, 0xF9431D29, 0xF9423129, 0x9136C129}, {28, 28, 24, 24}, {}},
        {{0x84334, 0x84DE0, 0x7AC150, 0x7AC538},
         {0xF9424529, 0xF9432929, 0xF9424529, 0x10E9EBE9}, {12, 12, 25, 25}, {}},
    }};
    return profiles::row(builds);
}

const PoseProfile& profile() {
    static constexpr std::array<PoseProfile, 9> builds{{
        {0x73DEB8, 0x73EB2C, 0x82518, 0x6FF6F0, 0x29D0944, 0x70ABC0,
         0xA10474, 0x7046C4, 0x29D782C, 0, 0x138, 0x15},
        {0x76177C, 0x7620E4, 0x824A8, 0x7505E8, 0x2A48CF4, 0x7554F4,
         0x9CFD5C, 0x68EC90, 0x2A4FF08, 0, 0x138, 0x15},
        {0x7B37A8, 0x7B55C0, 0x824A8, 0x744B58, 0x2A3FE64, 0x74CF44,
         0x9A55B8, 0x688C68, 0x2A47078, 0, 0x138, 0x15},
        {0x7480B0, 0x748DF8, 0x824A8, 0x715120, 0x2A33CC4, 0x71A8D4,
         0x909D9C, 0x6480F0, 0x2A3AED8, 0, 0x138, 0x15},
        {0x76B6EC, 0x76C078, 0x824A8, 0x74F450, 0x2A43014, 0x756E98,
         0x9960D8, 0x74C284, 0x2A4A228, 0, 0x138, 0x15},
        {0x1062984, 0x28396F0, 0x176560, 0x2839A00, 0x2846018, 0x1766F0,
         0x282EF38, 0x833490, 0x834840, 0x172330, 0xD8, 0x14},
        {0x10559B8, 0x282E1D0, 0x160240, 0x282E4E0, 0x283E1F8, 0x1603D0,
         0x2823A38, 0x836490, 0x83812C, 0x15C074, 0xD8, 0x14},
        {0x1052DA0, 0x282E4A8, 0xD2F10, 0x282E7B8, 0x283E578, 0xD30A0,
         0x2824874, 0x86B910, 0x86CDB0, 0xCED24, 0xD8, 0x14},
        {0x104F4F0, 0x283FFEC, 0x821E0, 0x28402FC, 0x284CE18, 0x1EF540,
         0x2835944, 0x77E760, 0x7802B4, 0x7DB70, 0xD8, 0x14},
    }};
    return profiles::row(builds);
}

constexpr std::array<std::array<std::uint32_t, 6>, 9> kHookWords{{
    {0xD10283FF, 0xD103C3FF, 0x79402C09, 0xD10283FF, 0xD101C3FF, 0xD100C3FF},
    {0xD10303FF, 0xD103C3FF, 0x79402C09, 0xD10283FF, 0xD10343FF, 0xD100C3FF},
    {0xD10283FF, 0xD103C3FF, 0x79402C09, 0xD10283FF, 0xD10383FF, 0xD100C3FF},
    {0xD10283FF, 0xD103C3FF, 0x79402C09, 0xD101C3FF, 0xD10343FF, 0xD100C3FF},
    {0xD10283FF, 0xD103C3FF, 0x79402C09, 0xD10283FF, 0xD10383FF, 0xD100C3FF},
    {0xD10503FF, 0xD10403FF, 0xF9400408, 0x79402408, 0x39407008, 0xD10343FF},
    {0xD10503FF, 0xD10403FF, 0xF9400408, 0x79402408, 0x39407008, 0xD10303FF},
    {0xD10503FF, 0xD10403FF, 0xF9400408, 0x79402408, 0x39407008, 0xD10303FF},
    {0xD10503FF, 0xD10403FF, 0xF9400408, 0x79402408, 0x39407008, 0xD10343FF},
}};

// 1.4.3's inlined bounds update rejoins here with the model in X19.
constexpr std::array<std::uintptr_t, 9> kFoldedBoundsSites{0, 0, 0, 0, 0, 0, 0, 0, 0x7CB38};
constexpr std::uint32_t kFoldedBoundsWord = 0xF9402274;

std::uintptr_t foldedBoundsSite() { return profiles::row(kFoldedBoundsSites); }

std::uintptr_t g_mainBase = 0;
alignas(pure::RenderFrameStore) std::byte g_storage[sizeof(pure::RenderFrameStore)];
pure::RenderFrameStore* g_frames = nullptr;
std::atomic<std::uint64_t> g_failure{0};
std::atomic<std::uint64_t> g_startEpoch{0};
std::atomic<std::uint32_t> g_frameGeneration{0};
std::atomic<std::uint64_t> g_latchedEpoch{0};
std::atomic<std::uint64_t> g_admittedEpoch{0};
std::atomic<std::uint32_t> g_preparedGeneration{0};
std::atomic<std::uint64_t> g_suppressedEpoch{0};
std::array<std::atomic<std::uintptr_t>, pure::kPoseModelLimit> g_liveEquipment{};

static_assert(sizeof(pure::RenderFrameStore) + sizeof(model::CaptureWorkspace) +
              sizeof(pure::PoseHistory) + sizeof(pure::PoseHistorySlot) * pure::kHistoryCapacity +
              pure::kPosePayloadArenaBytes <=
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
    if (!totk::engine::read_guard::admit(base, offset, sizeof(T))) return T{};
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
    return value;
}

pure::RenderFrameStore::Lease acquire(const void* unit, unsigned buffer) {
    if (!g_frames || !pose_session::active()) return {};
    const auto generation = g_frameGeneration.load(std::memory_order_acquire);
    if (!generation) return {};
    auto found = g_frames->acquireEpoch(reinterpret_cast<std::uintptr_t>(unit),
        frame::epoch(), generation);
    if (!found.lease) {
        if (found.owned && !equipment::publishedModel(unit) &&
            frame::epoch() > g_startEpoch.load(std::memory_order_acquire))
            fail(Failure::Buffer, buffer);
    }
    return std::move(found.lease);
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
    const auto buffer = read<std::uint8_t>(unit, profile().bufferIndex) & 3u;
    auto frame = acquire(unit, buffer);
    if (!frame) return;
    if (profile().inlineBeforeDrawRejoin && frame.uploaded(frame.modelIndex())) return;
    model::NativeRenderInput input;
    if (!makeInput(unit, frame, input)) return;
#if SELF_RECALL_STORAGE_PROFILE == 8
    std::optional<equipment::BodyAppearanceScope> bodyAppearance;
    const auto& animation = frame.get()->animation;
    if (frame.modelIndex() < animation.header.bodyModelCount) {
        const auto& id = animation.models[frame.modelIndex()].identity;
        bodyAppearance.emplace(animation.header.key, frame.modelIndex(),
            model::Identity{id.unit, id.skeleton, id.resource, id.boneCount, id.materialCount});
        if (!bodyAppearance->ready()) { fail(Failure::Prepare, 0xA002); return; }
    }
#endif
    using Calculate = void (*)(void*, unsigned);
    reinterpret_cast<Calculate>(g_mainBase + profile().calculateSkeleton)(input.skeleton, buffer);
    reinterpret_cast<Calculate>(g_mainBase + profile().calculateShape)(input.model, buffer);
    frame.markUploaded();
    monochrome::protectHistoricalModel(unit);
}

HOOK_DEFINE_TRAMPOLINE(BeforeDrawHook) {
    static std::uint64_t Callback(void* unit, void* context, const void* views, int viewCount) {
        const auto result = Orig(unit, context, views, viewCount);
        uploadHistoricalAnimation(unit);
        return result;
    }
};

HOOK_DEFINE_INLINE(InlineBeforeDrawRejoinHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        auto* gp = integerRegisters(ctx);
        auto* unit = reinterpret_cast<void*>(gp->X[19]);
        if (pose_session::active()) uploadHistoricalAnimation(unit);
        gp->X[8] = read<std::uint8_t>(unit, 0x14);
    }
};

HOOK_DEFINE_TRAMPOLINE(CalculateViewHook) {
    static void Callback(void* modelObject, unsigned view, const void* camera, unsigned buffer) {
        const auto* unit = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(modelObject) - profile().embeddedModel);
        auto frame = acquire(unit, buffer);
        model::NativeRenderInput input;
        if (frame && makeInput(unit, frame, input)) {
            Orig(input.model, view, camera, buffer);
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
                frame::epoch()) {
            for (const auto& live : g_liveEquipment)
                if (live.load(std::memory_order_relaxed) == reinterpret_cast<std::uintptr_t>(unit)) return 0;
        }
        return -1;
    }
    const auto visible = model::nativeShapeVisibility(unit,
        read<std::uint16_t>(renderUnit, 0x16), frame.get()->animation, frame.modelIndex());
    if (visible == pure::AnimationVisibility::Invalid) {
        fail(Failure::Visibility, 0x100u | frame.modelIndex());
        return 0;
    }
    return visible == pure::AnimationVisibility::Visible ? 1 : 0;
}
std::uint64_t skipHistoricalDraw(const void*, void*, unsigned, unsigned) { return 0; }

void routeHiddenDraw(exl::hook::InlineFloatCtx* ctx, unsigned siteIndex) {
    if (!pose_session::active()) return;
    auto* gp = integerRegisters(ctx);
    const auto& draw = inlineDrawProfile();
    const auto* unit = reinterpret_cast<const void*>(gp->X[draw.unitRegisters[siteIndex]]);
    if (unit && draw.unitOffsets[siteIndex])
        unit = read<const void*>(unit, draw.unitOffsets[siteIndex]);
    if (unit && recalledVisibility(unit) == 0)
        gp->X[8] = reinterpret_cast<std::uintptr_t>(&skipHistoricalDraw);
    // The trampoline replays LDR/ADD X9, so preserve its incoming address operand.
}

HOOK_DEFINE_INLINE(SingleWorkerDrawHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        routeHiddenDraw(ctx, 0);
    }
};
HOOK_DEFINE_INLINE(ArrayWorkerDrawHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        routeHiddenDraw(ctx, 1);
    }
};
HOOK_DEFINE_INLINE(SingleGroupDrawHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        routeHiddenDraw(ctx, 2);
    }
};
HOOK_DEFINE_INLINE(ArrayGroupDrawHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        routeHiddenDraw(ctx, 3);
    }
};

HOOK_DEFINE_TRAMPOLINE(ShapeIsVisibleHook) {
    static std::uint64_t Callback(const void* renderUnit) {
        const auto visible = recalledVisibility(renderUnit);
        return visible < 0 ? Orig(renderUnit) : static_cast<std::uint64_t>(visible);
    }
};

HOOK_DEFINE_TRAMPOLINE(ShapeDrawHook) {
    static std::uint64_t Callback(const void* unit, void* context, unsigned view, unsigned flags) {
        return drawVisible(unit) ? Orig(unit, context, view, flags) : 0;
    }
};
HOOK_DEFINE_TRAMPOLINE(ShapeArrayDrawHook) {
    static std::uint64_t Callback(const void* unit, void* context, unsigned view, unsigned flags) {
        return drawVisible(unit) ? Orig(unit, context, view, flags) : 0;
    }
};

using NativeCalculateBounding = float (*)(void*);
bool updateHistoricalBounds(void* unit, NativeCalculateBounding calculate) {
    if (g_admittedEpoch.load(std::memory_order_acquire) != frame::epoch()) return false;
    auto frame = acquire(unit, 0);
    if (!frame) return false;
#if SELF_RECALL_STORAGE_PROFILE == 8
    const auto* history = pose_storage::history();
    auto source = history ? history->acquire(frame.get()->animation.header.key) : pure::PoseReadLease{};
    if (!source) { fail(Failure::Prepare, 0xB001); return false; }
    const auto& recorded = *source.get();
#else
    const auto& recorded = frame.get()->animation;
#endif
    const auto& historical = recorded.models[frame.modelIndex()];
    model::View live;
    const auto described = model::describe(g_mainBase, unit, live);
    if (described != model::ViewStatus::Ready) { fail(Failure::Model, unsigned(described)); return false; }
    live.pose.originRelative = historical.originRelative;
    std::memcpy(live.pose.renderOrigin, historical.renderOrigin, sizeof(historical.renderOrigin));
    model::NativeRenderInput animation;
    const auto input = animation.prepare(live, historical, recorded.bones + historical.identity.firstBone);
    if (input != model::RenderInputStatus::Ready) { fail(Failure::Input, unsigned(input)); return false; }
    model::NativeBoundingInput bounds;
    bounds.prepareHistorical(unit, animation, pure::kHistoricalEquipment
        ? pure::translatedBoundsSpace(historical, frame.get()->rootOffset) : historical);
    (void)calculate(bounds.unit);
    frame.markBounded();
    return true;
}

HOOK_DEFINE_TRAMPOLINE(CalculateBoundingHook) {
    static float Callback(void* unit) {
        const auto result = Orig(unit);
        updateHistoricalBounds(unit, [](void* model) { return Orig(model); });
        return result;
    }
};

HOOK_DEFINE_INLINE(FoldedBoundingHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        auto* gp = integerRegisters(ctx);
        updateHistoricalBounds(reinterpret_cast<void*>(gp->X[19]),
            [](void* model) { return CalculateBoundingHook::Orig(model); });
    }
};

}

bool sitesValid(std::uintptr_t mainBase, std::size_t textSize) {
    const auto holds = [&](std::uintptr_t site, std::uint32_t word) {
        return !site || profiles::holds(mainBase, textSize, site, word);
    };
    const auto& sites = profile();
    const std::array<std::uintptr_t, 6> offsets{sites.beforeDraw, sites.calculateView,
        sites.shapeIsVisible, sites.calculateBounding, sites.shapeDraw, sites.shapeArrayDraw};
    if (!profiles::holds(mainBase, textSize, offsets, profiles::row(kHookWords)) ||
        !holds(sites.inlineBeforeDrawRejoin, 0x39405268) || !holds(foldedBoundsSite(), kFoldedBoundsWord))
        return false;
    const auto& draw = inlineDrawProfile();
    for (std::size_t i = 0; i < draw.sites.size(); ++i)
        if (!holds(draw.sites[i], draw.words[i])) return false;
    return true;
}

bool drawVisible(const void* renderUnit) {
    return renderUnit && recalledVisibility(renderUnit) != 0;
}

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    g_frames = ::new (static_cast<void*>(g_storage)) pure::RenderFrameStore;
    BeforeDrawHook::InstallAtOffset(profile().beforeDraw);
    if (profile().inlineBeforeDrawRejoin)
        InlineBeforeDrawRejoinHook::InstallAtOffset(profile().inlineBeforeDrawRejoin);
    CalculateViewHook::InstallAtOffset(profile().calculateView);
    ShapeIsVisibleHook::InstallAtOffset(profile().shapeIsVisible);
    CalculateBoundingHook::InstallAtOffset(profile().calculateBounding);
    ShapeDrawHook::InstallAtOffset(profile().shapeDraw);
    ShapeArrayDrawHook::InstallAtOffset(profile().shapeArrayDraw);
    if (foldedBoundsSite()) FoldedBoundingHook::InstallAtOffset(foldedBoundsSite());
    const auto& draw = inlineDrawProfile();
    if (draw.sites[0]) SingleWorkerDrawHook::InstallAtOffset(draw.sites[0]);
    if (draw.sites[1]) ArrayWorkerDrawHook::InstallAtOffset(draw.sites[1]);
    if (draw.sites[2]) SingleGroupDrawHook::InstallAtOffset(draw.sites[2]);
    if (draw.sites[3]) ArrayGroupDrawHook::InstallAtOffset(draw.sites[3]);
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
        {animation.models, animation.header.modelCount},
        profiles::newerRenderer() ? model::kNewAdmissionLayout : model::kLegacyAdmissionLayout);
    if (plan.status != model::AdmissionStatus::Ready) {
        fail(Failure::Admission, 0x300u | static_cast<unsigned>(plan.status)); return;
    }
    using Request = void (*)(const void*);
    for (unsigned i = 0; i < plan.count; ++i)
        reinterpret_cast<Request>(g_mainBase + profile().requestDraw)(plan.request[i]);
    for (unsigned i = 0; i < current.size(); ++i) {
        if (!animation.models[i].queueAdmission) continue;
        auto* unit = reinterpret_cast<std::byte*>(current[i].identity.unit);
        auto flags = read<std::uint16_t>(unit, 0x12);
        flags |= 0x200u;
        std::memcpy(unit + 0x12, &flags, sizeof(flags));
    }
    g_admittedEpoch.store(epoch, std::memory_order_release);
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
            fail(Failure::Visibility, 0x200u | static_cast<unsigned>(i));
            return;
        }
        if (visible == pure::AnimationVisibility::Visible) {
            if (!current[i].pose.queueAdmission) {
                fail(Failure::Admission, static_cast<unsigned>(i));
                return;
            }
            if (!currentFrame.lease.uploaded(static_cast<unsigned>(i))) {
                const auto* unit = reinterpret_cast<const void*>(b.unit);
                auto bounded = currentFrame.lease.bounded(static_cast<unsigned>(i));
                // The inlined 1.4.3 copy skips the bounds hook when a model has no bounding shapes.
                if (!bounded && foldedBoundsSite() && read<std::uint16_t>(unit, 0x24) == 0) bounded = true;
                // Repeat bounds and upload once before failing a model after the single-root pass.
                const bool lateRecovery = profiles::newerRenderer() && read<std::uint32_t>(unit, 0xC) != 0;
                if (lateRecovery && !bounded) {
                    using Calculate = float (*)(void*);
                    (void)reinterpret_cast<Calculate>(g_mainBase + profile().calculateBounding)(
                        const_cast<void*>(unit));
                    bounded = currentFrame.lease.bounded(static_cast<unsigned>(i));
                }
                if (lateRecovery && bounded) {
                    uploadHistoricalAnimation(const_cast<void*>(unit));
                    if (currentFrame.lease.uploaded(static_cast<unsigned>(i))) continue;
                }
                // A bounded model with an empty cull mask is off screen, not missing.
                if (bounded && read<std::uint32_t>(unit, 0xC) == 0) continue;
                Logging.Log("[self-recall] ANIMATION_UPLOAD_MISSING model=%u archive=%u bounded=%u "
                            "bounds_shapes=%u cull=%u flags=%u",
                    static_cast<unsigned>(i), unsigned(equipment::publishedModel(unit)), unsigned(bounded),
                    unsigned(read<std::uint16_t>(unit, 0x24)), read<std::uint32_t>(unit, 0xC),
                    unsigned(read<std::uint16_t>(unit, 0x12)));
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
    auto result = pure::RenderPrepareStatus::InvalidFrame;
#if SELF_RECALL_STORAGE_PROFILE == 7
    std::uint64_t report = 0;
    bool prepared = false;
    result = g_frames->begin(*selected.get(), epoch, presentation.offset,
        [&](pure::RecordedPoseFrame& candidate) {
            prepared = pose_recorder::prepareCurrentEquipment(candidate, *selected.get(), report);
            return prepared;
        });
    if (!prepared) {
        fail(Failure::Prepare, 0xC000u | unsigned(report >> 56)); return;
    }
#else
    result = g_frames->begin(*selected.get(), epoch, presentation.offset);
#endif
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
    out.epoch = frame::epoch();
    std::memcpy(out.matrix, header.wristMatrix, sizeof(out.matrix));
    pure::shiftMatrix(out.matrix, presentation.offset);
    return pose_session::active();
}
std::uint64_t takeFailure() { return g_failure.exchange(0, std::memory_order_acq_rel); }
void begin() {
    g_startEpoch.store(frame::epoch(), std::memory_order_release);
    g_frameGeneration.store(0, std::memory_order_release);
    g_preparedGeneration.store(0, std::memory_order_release);
}
bool ready(std::uint32_t generation) {
    return generation && g_preparedGeneration.load(std::memory_order_acquire) == generation;
}
void reset() {
    g_failure.store(0, std::memory_order_release);
    g_frameGeneration.store(0, std::memory_order_release);
    g_preparedGeneration.store(0, std::memory_order_release);
}

}
