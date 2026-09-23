#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "pure/FleetTelemetryMath.hpp"

namespace linked_stick::pure::matched {
using Vec = TelemetryVector;
using Mat = std::array<float, 9>;
inline constexpr float kMaximumMotionSpeed = 200.0f, kMaximumAngularSpeed = 32.0f;
inline constexpr float kMaximumMemberDelta = 64.0f, kMaximumAngularDelta = 2.1f;
inline constexpr float kCatchUpAcceleration = 60.0f, kHeightAcceleration = 40.0f;
inline constexpr float kOrbitAcceleration = 40.0f, kTotalHorizontalAcceleration = 100.0f;
inline constexpr float kAlignmentRate = 4.0f, kAlignmentAcceleration = 20.0f;
struct ActivityClock {
    std::uint64_t time = 0, serial = 0;
    float advance(std::uint64_t now, std::uint64_t nextSerial, std::uint64_t frequency) {
        if (nextSerial == serial) return 0;
        const auto before = time;
        time = now;
        serial = nextSerial;
        if (!frequency || now <= before) return 0;
        const float dt = static_cast<float>(now - before) / static_cast<float>(frequency);
        return dt <= 0.10f ? dt : 0;
    }
};
inline constexpr Mat identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
inline Vec add(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec mul(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec cross(Vec a, Vec b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec a) { return std::sqrt(dot(a, a)); }
inline Vec limit(Vec a, float cap) {
    const float n = length(a);
    return n > cap ? mul(a, cap / n) : a;
}
inline Vec effectiveVelocity(Vec simulated, Vec requested, bool pending) {
    return pending ? requested : simulated;
}
inline bool velocityDelivered(Vec expectedLinear, Vec expectedAngular, Vec actualLinear,
                              Vec actualAngular) {
    return isFinite(actualLinear) && isFinite(actualAngular) &&
           length(sub(actualLinear, expectedLinear)) < 0.05f &&
           length(sub(actualAngular, expectedAngular)) < 0.02f;
}
inline Vec column(const Mat& m, int c) { return {m[c], m[c + 3], m[c + 6]}; }
inline Vec rotate(const Mat& m, Vec v) {
    return add(add(mul(column(m, 0), v.x), mul(column(m, 1), v.y)), mul(column(m, 2), v.z));
}
inline Mat transpose(const Mat& m) {
    return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
}
inline Mat product(const Mat& a, const Mat& b) {
    Mat out{};
    for (int c = 0; c < 3; ++c) {
        const auto v = rotate(a, column(b, c));
        out[c] = v.x;
        out[c + 3] = v.y;
        out[c + 6] = v.z;
    }
    return out;
}
inline bool validRotation(const Mat& m) {
    for (float x : m)
        if (!std::isfinite(x)) return false;
    const auto a = column(m, 0), b = column(m, 1), c = column(m, 2);
    return std::fabs(dot(a, a) - 1) < 0.04f && std::fabs(dot(b, b) - 1) < 0.04f &&
           std::fabs(dot(c, c) - 1) < 0.04f && std::fabs(dot(a, b)) < 0.04f &&
           std::fabs(dot(a, c)) < 0.04f && std::fabs(dot(b, c)) < 0.04f &&
           dot(cross(a, b), c) > 0.96f;
}

inline Vec rotationError(const Mat& target, const Mat& current) {
    const Mat m = product(target, transpose(current));
    float w = 0, x = 0, y = 0, z = 0;
    const float trace = m[0] + m[4] + m[8];
    if (trace > 0) {
        const float s = 2 * std::sqrt(trace + 1);
        w = s / 4;
        x = (m[7] - m[5]) / s;
        y = (m[2] - m[6]) / s;
        z = (m[3] - m[1]) / s;
    } else {
        int i = 0;
        if (m[4] > m[0]) i = 1;
        if (m[8] > m[i * 3 + i]) i = 2;
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        const float s =
            2 * std::sqrt(std::max(0.0f, 1 + m[i * 3 + i] - m[j * 3 + j] - m[k * 3 + k]));
        if (s < 0.00001f) return {};
        float q[3]{};
        q[i] = s / 4;
        q[j] = (m[j * 3 + i] + m[i * 3 + j]) / s;
        q[k] = (m[k * 3 + i] + m[i * 3 + k]) / s;
        w = (m[k * 3 + j] - m[j * 3 + k]) / s;
        x = q[0];
        y = q[1];
        z = q[2];
    }
    if (w < 0) {
        w = -w;
        x = -x;
        y = -y;
        z = -z;
    }
    const Vec v{x, y, z};
    const float n = length(v);
    return n < 0.00001f ? mul(v, 2) : mul(v, 2 * std::atan2(n, w) / n);
}

struct ShapeMember {
    std::uint64_t kind = 0;
    Vec position{};
    Mat rotation = identity;
};
struct Shape {
    std::array<ShapeMember, 21> members{};
    std::uint32_t count = 0;
};
inline bool sameShape(const Shape& a, const Shape& b) {
    if (a.count < 2 || a.count > 21 || a.count != b.count) return false;
    std::array<bool, 21> used{};
    for (std::uint32_t i = 0; i < a.count; ++i) {
        if (!isFinite(a.members[i].position) || !validRotation(a.members[i].rotation)) return false;
        bool found = false;
        for (std::uint32_t j = 0; j < b.count; ++j) {
            if (used[j] || !isFinite(b.members[j].position) ||
                a.members[i].kind != b.members[j].kind ||
                length(sub(a.members[i].position, b.members[j].position)) > 0.20f ||
                !validRotation(a.members[i].rotation) || !validRotation(b.members[j].rotation) ||
                length(rotationError(a.members[i].rotation, b.members[j].rotation)) > 0.12f)
                continue;
            used[j] = true;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

struct Motion {
    Vec position{}, velocity{}, angular{};
    Mat rotation = identity;
    float radius = 1;
};
inline bool valid(const Motion& m) {
    return isFinite(m.position) && isFinite(m.velocity) && isFinite(m.angular) &&
           validRotation(m.rotation) && std::isfinite(m.radius) && m.radius > 0 &&
           length(m.velocity) < kMaximumMotionSpeed && length(m.angular) < kMaximumAngularSpeed;
}
inline bool horizontalFrame(const Motion& m, Mat& out, float& rate) {
    auto f = column(m.rotation, 2);
    const auto df = cross(m.angular, f);
    const float n = f.x * f.x + f.z * f.z;
    if (n < 0.25f) return false;
    rate = (f.z * df.x - f.x * df.z) / n;
    f = mul(Vec{f.x, 0, f.z}, 1 / std::sqrt(n));
    out = {f.z, 0, f.x, 0, 1, 0, -f.x, 0, f.z};
    return std::isfinite(rate);
}

enum class State : std::uint8_t { Waiting, Following, TooClose, Invalid };
struct Command {
    Vec linear{}, angular{}, error{};
    Vec turnAcceleration{}, feedbackAcceleration{};
    float yawRate = 0, yawAcceleration = 0;
    float angle = 0, speedError = 0;
    State state = State::Waiting;
    bool apply = false, vertical = false, limited = false;
    bool capturedLane = false;
    Vec initialOffset{}, laneOffset{};
};
class Controller {
   public:
    void reset() { *this = Controller{}; }
    void pauseTracking() { turnHistory_ = false; yawAcceleration_ = 0; }
    Command step(const Motion& guide, const Motion& follower, float dt, bool compactPair = true) {
        Command out{};
        Mat frame{};
        float yawRate = 0;
        if (!valid(guide) || !valid(follower) || !std::isfinite(dt) || dt <= 0 || dt > 0.10f ||
            !horizontalFrame(guide, frame, yawRate)) {
            pauseTracking();
            out.state = State::Invalid;
            return out;
        }
        const auto relative = sub(follower.position, guide.position);
        const float distance = length(relative);
        const float clearance = std::max(3.0f, guide.radius + follower.radius + 0.5f);
        if (!captured_) {
            offset_ = rotate(transpose(frame), relative);
            out.initialOffset = offset_;
            offset_.y = 0;
            const float rowTolerance = std::clamp(std::fabs(offset_.x) * 0.25f, 2.0f, 3.0f);
            const bool row = std::fabs(offset_.z) <= rowTolerance;
            if (row) {
                offset_.z = 0;
                const float side = offset_.x < 0 ? -1.0f : 1.0f;
                offset_.x = side * std::max(std::fabs(offset_.x), clearance + 0.5f);
            }
            const float horizontalDistance = length(offset_);
            if (compactPair && row && (horizontalDistance > 12 ||
                                      std::hypot(out.initialOffset.x, out.initialOffset.z) < clearance)) {
                // Acquire a compact target lane without changing pose; preserve multi-receiver spacing.
                const float side = offset_.x < 0 ? -1.0f : 1.0f;
                offset_ = {side * std::max(8.0f, clearance + 1), 0, 0};
            }
            captured_ = true;
            out.capturedLane = true;
            out.laneOffset = offset_;
        }
        // Capture before the spacing guard so launch motion cannot redefine the formation.
        if (!started_ && distance < clearance) {
            out.state = State::TooClose;
            return out;
        }
        started_ = true;
        const auto offset = rotate(frame, offset_);
        out.error = sub(add(guide.position, offset), follower.position);
        out.vertical = true;
        const Vec spin{0, yawRate, 0};
        const auto targetVelocity = add(guide.velocity, cross(spin, offset));
        const auto speedError = sub(targetVelocity, follower.velocity);
        out.speedError = length(speedError);
        // Native input supplies common thrust; add only relative orbital acceleration.
        const float rawAlpha = turnHistory_ ? std::clamp((yawRate - previousYawRate_) / dt, -3.0f, 3.0f) : 0;
        yawAcceleration_ += (rawAlpha - yawAcceleration_) * (1 - std::exp(-dt / 0.12f));
        previousYawRate_ = yawRate;
        turnHistory_ = true;
        out.yawRate = yawRate;
        out.yawAcceleration = yawAcceleration_;
        const auto rawTurn = add(cross(Vec{0, yawAcceleration_, 0}, offset), cross(spin, cross(spin, offset)));
        out.turnAcceleration = limit(rawTurn, kOrbitAcceleration);
        const auto closing = limit(Vec{out.error.x * 3, 0, out.error.z * 3}, 45.0f);
        const Vec closingVelocity{closing.x, std::clamp(out.error.y * 3, -30.0f, 30.0f), closing.z};
        const float response = (1 - std::exp(-12.0f * dt)) / dt;
        const auto candidateBias = limit(add(bias_, mul(out.error, 4.0f * dt)), 16.0f);
        const auto feedback = add(mul(add(speedError, closingVelocity), response), candidateBias);
        const auto horizontalFeedback = limit(Vec{feedback.x, 0, feedback.z}, kCatchUpAcceleration);
        out.feedbackAcceleration = {horizontalFeedback.x,
            std::clamp(feedback.y, -kHeightAcceleration, kHeightAcceleration), horizontalFeedback.z};
        const auto acceleration = add(out.feedbackAcceleration, out.turnAcceleration);
        const auto horizontal = limit(Vec{acceleration.x, 0, acceleration.z}, kTotalHorizontalAcceleration);
        const Vec bounded{horizontal.x, acceleration.y, horizontal.z};
        const auto excess = add(sub(feedback, out.feedbackAcceleration), sub(acceleration, bounded));
        if (dot(out.error, excess) <= 0.0001f) bias_ = candidateBias;
        auto dv = mul(add(feedback, rawTurn), dt);
        out.linear = mul(bounded, dt);
        const auto angle = rotationError(guide.rotation, follower.rotation);
        out.angle = length(angle);
        const auto desiredAngular = add(guide.angular, limit(mul(angle, 6.0f), kAlignmentRate));
        auto dw = mul(sub(desiredAngular, follower.angular), response * dt);
        out.angular = limit(dw, kAlignmentAcceleration * dt);
        if (distance < clearance && distance > 0.001f) {
            const auto away = mul(relative, 1 / distance);
            const float inward = dot(out.linear, away);
            if (inward < 0) out.linear = sub(out.linear, mul(away, inward));
        }
        out.limited =
            length(sub(out.linear, dv)) > 0.0001f || length(sub(out.angular, dw)) > 0.0001f;
        out.apply = true;
        out.state = State::Following;
        return out;
    }

   private:
    Vec offset_{};
    Vec bias_{};
    float previousYawRate_ = 0, yawAcceleration_ = 0;
    bool turnHistory_ = false;
    bool captured_ = false;
    bool started_ = false;
};

// Use simulation time, never controller sampling or host wall time.
inline bool simulationPass(bool modeValid, unsigned mode, bool worldValid, unsigned world,
                           float dt) {
    return modeValid && (mode & 5u) == 5u && (!worldValid || world == 0) && std::isfinite(dt) &&
           dt > 0 && dt <= 0.10f;
}

class FormationHealth {
   public:
    const char* step(const Command& c, bool delivered, float dt) {
        if (!delivered)
            faultSeconds_ = 1.0f;
        else
            faultSeconds_ = std::max(0.0f, faultSeconds_ - dt);
        if (faultSeconds_ > 0) {
            settledSeconds_ = 0;
            return "physics mismatch";
        }
        const float error = length(c.error);
        const bool settled = error < 0.75f && c.angle < 0.12f && c.speedError < 0.75f;
        settledSeconds_ = settled ? std::min(1.0f, settledSeconds_ + dt) : 0;
        if (error > 2.0f) return "drifting";
        if (settledSeconds_ >= 0.5f) return "holding";
        return "correcting";
    }

   private:
    float settledSeconds_ = 0, faultSeconds_ = 0;
};

// Rigid rotation requires tangential velocity changes at every member's centre of mass.
inline Vec memberDelta(Vec linear, Vec angular, Vec memberCenter, Vec assemblyCenter) {
    return add(linear, cross(angular, sub(memberCenter, assemblyCenter)));
}
}
