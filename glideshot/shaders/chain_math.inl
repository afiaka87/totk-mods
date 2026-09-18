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
