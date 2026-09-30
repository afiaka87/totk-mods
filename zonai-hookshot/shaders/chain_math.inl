// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// The identities the chain shader and the host tests share, written in the subset both GLSL and C++ accept.
#ifndef CH_INLINE
#define CH_INLINE
#endif

CH_INLINE float chWorldParameter(float t, float nearZ, float farZ) {
    float denominator = farZ + t * (nearZ - farZ);
    return chAbs(denominator) < 0.000000001f ? t : (t * nearZ) / denominator;
}

CH_INLINE float chViewDepth(float t, float nearZ, float farZ) {
    float denominator = farZ + t * (nearZ - farZ);
    return chAbs(denominator) < 0.000000001f ? nearZ : (nearZ * farZ) / denominator;
}

CH_INLINE float chHelixAngle(float worldParameter, float spanMetres,
                             float wavelength, float phaseRadians) {
    return wavelength > 0.0001f
        ? 6.283185307f * (worldParameter * spanMetres) / wavelength + phaseRadians
        : phaseRadians;
}

CH_INLINE float chHandTaper(float metres) {
    float t = metres / 0.45f;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

CH_INLINE float chHandTaperSlope(float metres) {
    float t = metres / 0.45f;
    if (t <= 0.0f || t >= 1.0f) return 0.0f;
    return 6.0f * t * (1.0f - t) / 0.45f;
}

// Shared conservative extent for the ribbon halos and orbiting motes, in pixels.
CH_INLINE float chEnergyPadding(float halfWidth, float feather) {
    return 8.0f * halfWidth + 3.0f * feather + 2.0f;
}

CH_INLINE float chReticlePadding(float halfWidth, float feather) {
    return 4.0f * halfWidth + 3.0f * feather + 2.0f;
}
