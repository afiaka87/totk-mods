// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Chain and reticle evaluated per pixel in flipped pixel space (x right, y down); chain_math.inl holds the shared identities.
#version 450

layout(location = 0) in vec2 screenUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneDepth;
layout(std140, binding = 0) uniform Chain {
    vec4 dimensions;    // x width px, y height px, z near m, w far m
    vec4 flags;         // x uniformsValid, y bodyVisible, z reticleVisible, w occludedGain
    vec4 nearPoint;     // xy pixel, z viewZ m, w radialA view-space z
    vec4 farPoint;      // xy pixel, z viewZ m, w drawn span in metres
    vec4 radial;        // xy radialA view xy, zw radialB view xy
    vec4 coil;          // x radius m, y wavelength m, z phase rad, w strand half px
    vec4 pxScale;       // xy world->px at unit depth, z spine half px, w feather px
    vec4 coreA;         // rgb + coverage
    vec4 coreB;
    vec4 spineColor;    // near-black structural core
    vec4 reticle;       // xy pixel centre, z viewZ, w outer radius px
    vec4 reticleShape;  // x inner scale, y ring half px, z underlay half px, w radialB view z
    vec4 reticleColor;  // valid/invalid AND the latch pulse already mixed on the CPU
    vec4 reticleUnder;  // dark underlay
};

const float kTau = 6.283185307;

const float kCoilOpenness = 0.25;

float chAbs(float x) { return abs(x); }
// @include chain_math.inl

vec4 over(vec4 acc, vec4 top) { return top + acc * (1.0 - top.a); }

float stripe(float d, float w, float feather) {
    return 1.0 - smoothstep(w - feather, w + feather, d);
}

void main() {
    vec2 texel = vec2(screenUv.x, 1.0 - screenUv.y) * dimensions.xy;
    ivec2 pixel = clamp(ivec2(texel), ivec2(0), ivec2(dimensions.xy) - ivec2(1));
    float feather = pxScale.w;

    if (flags.x < 0.5) {
        discard;                                  // uniforms not trustworthy this frame
    } else {
        float rawDepth = texelFetch(sceneDepth, pixel, 0).r;
        bool depthOk = !isnan(rawDepth) && !isinf(rawDepth) &&
                       rawDepth >= 0.0 && rawDepth < 1.0;
        float sceneZ = depthOk
            ? dimensions.z + rawDepth * (dimensions.w - dimensions.z)
            : dimensions.w;

        vec4 acc = vec4(0.0);                     // premultiplied accumulator

        if (flags.y > 0.5) {
            vec2 axis = farPoint.xy - nearPoint.xy;
            float axisLen = max(length(axis), 0.001);
            vec2 alongDir = axis / axisLen;
            vec2 acrossDir = vec2(-alongDir.y, alongDir.x);
            float z0 = nearPoint.z;
            float z1 = farPoint.z;

            vec2 rel = texel - nearPoint.xy;
            float along = dot(rel, alongDir);
            float across = dot(rel, acrossDir);
            float t = clamp(along / axisLen, 0.0, 1.0);

            float u = chWorldParameter(t, z0, z1);
            float z = chViewDepth(t, z0, z1);
            float theta = chHelixAngle(u, farPoint.w, coil.y, coil.z);
            float c = cos(theta);
            float s = sin(theta);
            float radialZ = c * nearPoint.w + s * reticleShape.w;

            float amplitude = coil.x / z;
            vec2 radialAPx = pxScale.xy * radial.xy;
            vec2 radialBPx = pxScale.xy * radial.zw;
            float acrossA = dot(radialAPx, acrossDir);
            float acrossB = dot(radialBPx, acrossDir);

            float dThetaDu = coil.y > 0.0001 ? kTau * farPoint.w / coil.y : 0.0;
            float dThetaDAlong = dThetaDu * ((z * z) / (z0 * z1)) / axisLen;
            float turnPx = kTau / max(dThetaDAlong, 0.000001);

            float swing = amplitude * sqrt(acrossA * acrossA + acrossB * acrossB);
            float openness = kCoilOpenness * turnPx;
            float scale = swing > openness ? openness / max(swing, 0.000001) : 1.0;

            float offset = scale * amplitude * (c * acrossA + s * acrossB);

            float dOffsetDTheta = scale * amplitude * (-s * acrossA + c * acrossB);
            float slope = dOffsetDTheta * dThetaDAlong;
            float shrink = inversesqrt(1.0 + slope * slope);

            float overshoot = max(0.0, max(-along, along - axisLen));
            float overshootSq = overshoot * overshoot;

            float vA = (across - offset) * shrink;
            float vB = (across + offset) * shrink;
            float dA = sqrt(vA * vA + overshootSq);
            float dB = sqrt(vB * vB + overshootSq);
            float dS = sqrt(across * across + overshootSq);

            float covS = stripe(dS, pxScale.z, feather) * spineColor.a;
            float covA = stripe(dA, coil.w, feather) * coreA.a;
            float covB = stripe(dB, coil.w, feather) * coreB.a;

            float vis = (depthOk && z > sceneZ + 0.05) ? flags.w : 1.0;

            float lean = clamp(radialZ, -1.0, 1.0);
            float shadeA = mix(0.62, 1.0, 0.5 + 0.5 * lean);
            float shadeB = mix(0.62, 1.0, 0.5 - 0.5 * lean);
            acc = over(acc, vec4(spineColor.rgb * covS, covS));

            vec4 layerA = vec4(coreA.rgb * shadeA * covA, covA);
            vec4 layerB = vec4(coreB.rgb * shadeB * covB, covB);
            if (radialZ > 0.0) {
                acc = over(acc, layerB);
                acc = over(acc, layerA);
            } else {
                acc = over(acc, layerA);
                acc = over(acc, layerB);
            }

            acc *= vis;
        }

        if (flags.z > 0.5) {
            float r = length(texel - reticle.xy);
            float outer = reticle.w;
            float dOuter = abs(r - outer);
            float dInner = abs(r - outer * reticleShape.x);

            float covU = stripe(dOuter, reticleShape.z, feather) * reticleUnder.a;
            float covO = stripe(dOuter, reticleShape.y, feather) * reticleColor.a;
            float covI = stripe(dInner, reticleShape.y * 0.78, feather) * reticleColor.a;

            acc = over(acc, vec4(reticleUnder.rgb * covU, covU));
            acc = over(acc, vec4(reticleColor.rgb * covO, covO));
            acc = over(acc, vec4(reticleColor.rgb * covI, covI));
        }

        if (acc.a < 0.002 && max(acc.r, max(acc.g, acc.b)) < 0.002) {
            discard;                              // the cheap majority path
        } else {
            outColor = acc;                       // premultiplied; blend is ONE / ONE_MINUS_SRC_ALPHA
        }
    }
}
