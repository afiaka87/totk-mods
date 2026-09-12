#pragma once

#include <cmath>
#include <cstdint>

namespace self_recall::pure {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 mul(Vec3 a, float scalar) {
    return {a.x * scalar, a.y * scalar, a.z * scalar};
}
constexpr float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float length(Vec3 value) { return std::sqrt(dot(value, value)); }
inline bool finite3(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
constexpr float clampRange(float value, float minimum, float maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

constexpr std::uint16_t kMaxRenderPoints = 160;

struct RoutePoint {
    Vec3 position{};
    std::uint16_t ordinal = 0;
};

struct RenderRoute {
    std::uint16_t count = 0;
    RoutePoint points[kMaxRenderPoints]{};
};

struct SimplifyParams {
    float duplicateEpsilon = 0.004f;
    float turnKeepRadians = 0.16f;
    float verticalKeepMeters = 0.30f;
    float straightSpacingMeters = 1.6f;
};

constexpr SimplifyParams kDefaultSimplifyParams{};
constexpr int kSimplifyAttempts = 3;

struct RouteBuilder {
    RenderRoute route{};
    SimplifyParams params{};
    Vec3 previousSample{};
    Vec3 previousDirection{};
    Vec3 lastFedPosition{};
    std::uint16_t lastFedOrdinal = 0;
    float arcSinceKept = 0.0f;
    float headingSinceKept = 0.0f;
    float verticalSinceKept = 0.0f;
    std::uint8_t lastStateBits = 0;
    bool haveFed = false;
    bool haveDirection = false;
    std::uint32_t fed = 0;
    std::uint32_t duplicates = 0;
    std::uint32_t overflowWanted = 0;
};

inline void routeBegin(RouteBuilder& builder, const SimplifyParams& params) {
    builder = {};
    builder.params = params;
}

inline void routeStoreKeep(RouteBuilder& builder, Vec3 position,
                           std::uint16_t ordinal) {
    if (builder.route.count < kMaxRenderPoints) {
        builder.route.points[builder.route.count].position = position;
        builder.route.points[builder.route.count].ordinal = ordinal;
        ++builder.route.count;
    } else {
        ++builder.overflowWanted;
    }
    builder.arcSinceKept = 0.0f;
    builder.headingSinceKept = 0.0f;
    builder.verticalSinceKept = 0.0f;
}

inline void routeFeed(RouteBuilder& builder, Vec3 position,
                      std::uint16_t ordinal, std::uint8_t stateBits) {
    if (!finite3(position)) {
        return;
    }
    ++builder.fed;
    builder.lastFedPosition = position;
    builder.lastFedOrdinal = ordinal;

    if (!builder.haveFed) {
        builder.haveFed = true;
        builder.previousSample = position;
        builder.lastStateBits = stateBits;
        routeStoreKeep(builder, position, ordinal);
        return;
    }

    const Vec3 step = sub(position, builder.previousSample);
    const float stepLength = length(step);
    const bool transition = stateBits != builder.lastStateBits;
    if (!transition && stepLength < builder.params.duplicateEpsilon) {
        ++builder.duplicates;
        return;
    }

    if (stepLength >= builder.params.duplicateEpsilon) {
        const Vec3 direction = mul(step, 1.0f / stepLength);
        if (builder.haveDirection) {
            const float cosine = clampRange(
                dot(direction, builder.previousDirection), -1.0f, 1.0f);
            builder.headingSinceKept += std::acos(cosine);
        }
        builder.previousDirection = direction;
        builder.haveDirection = true;
        builder.arcSinceKept += stepLength;
        builder.verticalSinceKept += step.y < 0.0f ? -step.y : step.y;
        builder.previousSample = position;
    }

    const bool keep =
        transition ||
        builder.headingSinceKept >= builder.params.turnKeepRadians ||
        builder.verticalSinceKept >= builder.params.verticalKeepMeters ||
        builder.arcSinceKept >= builder.params.straightSpacingMeters;
    if (keep) {
        builder.lastStateBits = stateBits;
        routeStoreKeep(builder, position, ordinal);
    }
}

inline void routeFinish(RouteBuilder& builder) {
    if (!builder.haveFed || builder.route.count == 0) return;
    RoutePoint& tail = builder.route.points[builder.route.count - 1];
    if (tail.ordinal == builder.lastFedOrdinal) return;
    if (builder.route.count < kMaxRenderPoints) {
        builder.route.points[builder.route.count] = {
            builder.lastFedPosition, builder.lastFedOrdinal};
        ++builder.route.count;
    } else {
        tail = {builder.lastFedPosition, builder.lastFedOrdinal};
    }
}

inline SimplifyParams scaleSimplifyParams(const SimplifyParams& params,
                                          std::uint32_t wantedKeeps) {
    float factor = static_cast<float>(wantedKeeps) /
                   static_cast<float>(kMaxRenderPoints - 8);
    factor = clampRange(factor, 1.3f, 8.0f);
    SimplifyParams scaled = params;
    scaled.turnKeepRadians =
        clampRange(params.turnKeepRadians * factor, 0.0f, 3.0f);
    scaled.verticalKeepMeters = params.verticalKeepMeters * factor;
    scaled.straightSpacingMeters = params.straightSpacingMeters * factor;
    return scaled;
}

struct SimplifyResult {
    std::uint32_t fed = 0;
    std::uint32_t kept = 0;
    std::uint32_t duplicates = 0;
    std::uint32_t truncated = 0;
    int attempts = 0;
};

template <typename FeedFn>
inline SimplifyResult buildRoute(RenderRoute& out, FeedFn&& feed) {
    RouteBuilder builder{};
    SimplifyParams params = kDefaultSimplifyParams;
    SimplifyResult result{};
    for (int attempt = 0; attempt < kSimplifyAttempts; ++attempt) {
        routeBegin(builder, params);
        feed(builder);
        routeFinish(builder);
        result.attempts = attempt + 1;
        if (builder.overflowWanted == 0) break;
        params = scaleSimplifyParams(
            params, builder.route.count + builder.overflowWanted);
    }
    out = builder.route;
    result.fed = builder.fed;
    result.kept = builder.route.count;
    result.duplicates = builder.duplicates;
    result.truncated = builder.overflowWanted;
    return result;
}

}

#include "RecallPoseData.hpp"

namespace self_recall::pure {

enum class AnimationVisibility : std::uint8_t { Hidden, Visible, Invalid };

inline AnimationVisibility animationShapeVisibility(const RecordedPoseFrame& frame,
        unsigned modelIndex, unsigned boneIndex, unsigned materialIndex) {
    if (frame.header.modelCount > kPoseModelLimit || modelIndex >= frame.header.modelCount ||
        frame.header.boneCount > kPoseBoneLimit || frame.header.materialCount > kPoseMaterialLimit)
        return AnimationVisibility::Invalid;
    const auto& model = frame.models[modelIndex];
    const auto& id = model.identity;
    if (boneIndex >= id.boneCount || materialIndex >= id.materialCount ||
        id.firstBone + id.boneCount > frame.header.boneCount ||
        id.firstMaterial + id.materialCount > frame.header.materialCount || model.queueAdmission > 1)
        return AnimationVisibility::Invalid;
    if (!model.queueAdmission) return AnimationVisibility::Hidden;
    return visibilityBit(frame.visible.bones, static_cast<std::uint16_t>(id.firstBone + boneIndex)) &&
           visibilityBit(frame.visible.materials, static_cast<std::uint16_t>(id.firstMaterial + materialIndex))
         ? AnimationVisibility::Visible : AnimationVisibility::Hidden;
}

}

#include <cstddef>
#include <cstring>
#include <limits>
#include <span>

namespace self_recall::pure {

inline constexpr std::size_t kMonochromeFilterBytes = 0x90;

enum class MonochromeFilterStatus : unsigned { Ready, InvalidSize, OverlappingBuffers };

inline MonochromeFilterStatus buildMonochromeFilter(
        std::span<const std::byte> native, std::span<std::byte> destination) {
    if (native.size() != kMonochromeFilterBytes ||
        destination.size() != kMonochromeFilterBytes ||
        !native.data() || !destination.data())
        return MonochromeFilterStatus::InvalidSize;
    const auto source = reinterpret_cast<std::uintptr_t>(native.data());
    const auto target = reinterpret_cast<std::uintptr_t>(destination.data());
    const auto distance = source > target ? source - target : target - source;
    if (distance < kMonochromeFilterBytes)
        return MonochromeFilterStatus::OverlappingBuffers;

    std::uint32_t flags;
    std::memcpy(&flags, native.data() + 0x64, sizeof(flags));
    flags = (flags | 1u) & ~0x10u;
    std::memcpy(destination.data(), native.data(), kMonochromeFilterBytes);
    std::memcpy(destination.data() + 0x64, &flags, sizeof(flags));
    const float complete = 1.0f;
    const float character = 0.375f;
    const float other = 0.25f;
    std::memcpy(destination.data() + 0x1C, &complete, sizeof(complete));
    std::memcpy(destination.data() + 0x58, &complete, sizeof(complete));
    std::memcpy(destination.data() + 0x60, &complete, sizeof(complete));
    std::memcpy(destination.data() + 0x7C, &character, sizeof(character));
    std::memcpy(destination.data() + 0x80, &other, sizeof(other));
    return MonochromeFilterStatus::Ready;
}

enum class ObjectAttributeStatus : unsigned { Ready, InvalidValue, EncodingFailed };

template<class Set, class Calculate>
void uploadMonochromeAttribute(float original, float excluded, Set set, Calculate calculate) {
    set(excluded);
    calculate();
    if (excluded != original) set(original);
}

inline ObjectAttributeStatus enableMonochromeExclusion(float source, float& destination) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    constexpr float maximum = 65535.0f / 255.0f;
    if (!std::isfinite(source) || source < 0.0f || source > maximum)
        return ObjectAttributeStatus::InvalidValue;
    const float scaled = source * 255.0f;
    const auto flags = static_cast<std::uint32_t>(scaled);
    if (flags & 0x80u) {
        destination = source;
        return ObjectAttributeStatus::Ready;
    }
    const auto excluded = flags | 0x80u;
    float encoded = static_cast<float>(excluded) / 255.0f;
    if (static_cast<std::uint32_t>(encoded * 255.0f) < excluded)
        encoded = std::nextafter(encoded, std::numeric_limits<float>::infinity());
    if (encoded > maximum || static_cast<std::uint32_t>(encoded * 255.0f) != excluded)
        return ObjectAttributeStatus::EncodingFailed;
    destination = encoded;
    return ObjectAttributeStatus::Ready;
}

}

namespace self_recall::pure {

enum class PaletteParameter : unsigned { Character, Other };

struct PaletteWriterIdentity {
    std::uintptr_t model = 0;
    std::uintptr_t material = 0;
    std::uintptr_t resource = 0;
    bool operator==(const PaletteWriterIdentity&) const = default;
    explicit operator bool() const { return model && material && resource; }
};

struct PalettePreparedBuffer {
    PaletteWriterIdentity identity{};
    std::uint64_t epoch = 0;
    std::uintptr_t buffer = 0;
    unsigned index = 0;
    std::uint32_t bytes = 0;
    bool matches(PaletteWriterIdentity current, std::uint64_t currentEpoch,
                 std::uintptr_t currentBuffer, unsigned currentIndex, std::uint32_t currentBytes) const {
        return identity && identity == current && epoch && epoch == currentEpoch &&
            buffer && buffer == currentBuffer && index < 3 && index == currentIndex &&
            bytes && bytes <= UINT16_MAX && bytes == currentBytes;
    }
};

class PaletteWrites {
public:
    void beginFrame(std::uint64_t epoch, bool recall) {
        epoch_ = epoch;
        recall_ = recall && epoch;
        identity_ = {};
        mask_ = 0;
        indices_[0] = indices_[1] = -1;
    }
    bool record(std::uint64_t epoch, PaletteWriterIdentity identity,
                int materialIndex, PaletteParameter parameter, int parameterIndex,
                float appliedValue) {
        if (!epoch || epoch != epoch_) return false;
        if (!identity || materialIndex != 0 || parameterIndex < 0 ||
            !std::isfinite(appliedValue) || appliedValue < 0 || appliedValue > 1 ||
            static_cast<unsigned>(parameter) > 1) { mask_ = 0; return false; }
        if (identity != identity_) {
            identity_ = identity;
            mask_ = 0;
            indices_[0] = indices_[1] = -1;
        }
        const auto i = static_cast<unsigned>(parameter);
        if (i == 0) mask_ = 0;
        indices_[i] = parameterIndex;
        values_[i] = appliedValue;
        mask_ |= 1u << i;
        return true;
    }
    bool complete(std::uint64_t epoch, PaletteWriterIdentity freshIdentity) const {
        return epoch && epoch == epoch_ && mask_ == 3 && freshIdentity == identity_ &&
            indices_[0] != indices_[1];
    }
    bool recall(std::uint64_t epoch) const { return epoch && epoch == epoch_ && recall_; }
    int index(PaletteParameter parameter) const { return indices_[static_cast<unsigned>(parameter)]; }
    float value(PaletteParameter parameter) const { return values_[static_cast<unsigned>(parameter)]; }

private:
    PaletteWriterIdentity identity_{};
    std::uint64_t epoch_ = 0;
    int indices_[2]{-1, -1};
    float values_[2]{};
    unsigned mask_ = 0;
    bool recall_ = false;
};

}

namespace self_recall::pure {
struct CameraVerticalCorrection {
    float cachedRootDelta = 0;
    float transitionDelta = 0;
    float appliedDelta = 0;
};

inline bool alignCameraHeight(float* camera, float presentedRootY, float cachedRootY,
                              float idealLookY, CameraVerticalCorrection& correction) {
    if (!camera || !std::isfinite(presentedRootY) || !std::isfinite(cachedRootY) ||
        !std::isfinite(idealLookY)) return false;
    for (unsigned i = 0; i < 10; ++i)
        if (!std::isfinite(camera[i])) return false;
    const float rootDelta = presentedRootY - cachedRootY;
    const float transitionDelta = idealLookY - camera[4];
    const float targetY = idealLookY + rootDelta;
    const float delta = targetY - camera[4];
    const float eyeY = camera[1] + delta;
    if (!std::isfinite(rootDelta) || !std::isfinite(transitionDelta) ||
        !std::isfinite(targetY) || !std::isfinite(delta) || !std::isfinite(eyeY)) return false;
    camera[1] = eyeY;
    camera[4] = targetY;
    correction = {rootDelta, transitionDelta, delta};
    return true;
}
}

#include <atomic>

namespace self_recall::pure {
template<class Sample = PoseFrameKey>
class PresentationFrameLatch {
    std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::uint64_t session_ = 0, clock_ = 0;
    Sample key_{};
public:
    Sample publish(std::uint64_t session, std::uint64_t clock, Sample candidate) {
        if (!session || !clock) return {};
        while (gate_.test_and_set(std::memory_order_acquire)) {}
        Sample result;
        if (session == session_ && clock == clock_) result = key_;
        else if (candidate && (session > session_ || (session == session_ && clock > clock_))) {
            session_ = session;
            clock_ = clock;
            key_ = candidate;
            result = key_;
        }
        gate_.clear(std::memory_order_release);
        return result;
    }
    Sample snapshot(std::uint64_t session) {
        while (gate_.test_and_set(std::memory_order_acquire)) {}
        const auto result = session == session_ ? key_ : Sample{};
        gate_.clear(std::memory_order_release);
        return result;
    }
};
}
