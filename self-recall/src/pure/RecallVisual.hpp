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
inline float distance(Vec3 a, Vec3 b) { return length(sub(a, b)); }
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
    std::uint32_t rejectedNonFinite = 0;
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
        ++builder.rejectedNonFinite;
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
    std::uint32_t rejectedNonFinite = 0;
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
    result.rejectedNonFinite = builder.rejectedNonFinite;
    result.truncated = builder.overflowWanted;
    return result;
}

} // namespace self_recall::pure
