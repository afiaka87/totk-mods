// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>

namespace remote_bomb::blast {
struct Vector { float x{}, y{}, z{}; };
struct Pressure { Vector direction; float strength{}; };
struct TargetGeometry { float radius{}, projectedArea{}; };

inline TargetGeometry targetGeometry(float volume) {
    constexpr float pi = 0x1.921fb6p+1f, fourPi = 0x1.921fb6p+3f;
    const float radiusSquared = std::pow((volume * 3.f) / fourPi, 0x1.555556p-1f);
    const float surfaceArea = radiusSquared * fourPi;
    const float projectedArea = surfaceArea * .25f;
    return {std::sqrt(projectedArea / pi), projectedArea};
}

inline Vector incidentVelocity(Pressure pressure) {
    const float speed = pressure.strength * 340.f;
    return {pressure.direction.x * speed, pressure.direction.y * speed, pressure.direction.z * speed};
}

inline Vector pressureImpulse(Vector velocity, float projectedArea, float occlusion, float responseScale,
                              bool speedLimited = false, Vector background = {}) {
    if (velocity.x == 0.f && velocity.y == 0.f && velocity.z == 0.f)
        return background;
    float speed = std::sqrt((velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z);
    Vector direction = velocity;
    if (speed > 0.f) {
        const float inverseSpeed = 1.f / speed;
        direction = {velocity.x * inverseSpeed, velocity.y * inverseSpeed, velocity.z * inverseSpeed};
    }
    if (speedLimited)
        speed = std::fmin(speed, 600.f);
    const float pressure = (speed * speed) * 0x1.99999ap-5f;
    const Vector projected = {projectedArea * (direction.x * pressure),
                              projectedArea * (direction.y * pressure),
                              projectedArea * (direction.z * pressure)};
    const Vector occluded = {occlusion * projected.x, occlusion * projected.y, occlusion * projected.z};
    return {background.x + responseScale * occluded.x,
            background.y + responseScale * occluded.y,
            background.z + responseScale * occluded.z};
}

// BotW 1.5.0's blast pressure calculation, with its float constants unchanged.
inline Pressure pressureWave(Vector displacement, float targetRadius, float speed, bool obstructed = false) {
    if (targetRadius <= 0.f || obstructed ||
        (displacement.x == 0.f && displacement.y == 0.f && displacement.z == 0.f))
        return {};
    const float distance = std::sqrt((displacement.x * displacement.x + displacement.y * displacement.y) +
                                     displacement.z * displacement.z);
    Vector direction = displacement;
    if (distance > 0.f) {
        const float inverseDistance = 1.f / distance;
        direction = {inverseDistance * direction.x, inverseDistance * direction.y, inverseDistance * direction.z};
    }
    const float surfaceDistance = std::fmax(distance - targetRadius, 1.f);
    const float remainingSpeed = surfaceDistance > 1.f ? speed / surfaceDistance : speed;
    if (remainingSpeed < 1.f)
        return {direction, 0.f};
    const float projection = std::sqrt(surfaceDistance * ((targetRadius + targetRadius) + surfaceDistance));
    const float coverage = ((targetRadius / (surfaceDistance + targetRadius)) * projection) / targetRadius;
    const float speedSquared = remainingSpeed * remainingSpeed;
    const float pressureRatio = (speedSquared * 0x1.999998p-2f + 2.f) /
                                (speedSquared * 0x1.666666p+1f - 0x1.999998p-2f);
    const float residualSpeed = std::sqrt(std::fmax(pressureRatio, 0.f));
    return {direction, (remainingSpeed - residualSpeed) * (coverage * coverage)};
}
}
