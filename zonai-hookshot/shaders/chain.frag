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
    vec4 spineColor;    // faint guide filament
    vec4 reticle;       // xy pixel centre, z viewZ, w outer radius px
    vec4 reticleShape;  // x inner scale, y ring half px, z underlay half px, w radialB view z
    vec4 reticleColor;  // valid/invalid AND the latch pulse already mixed on the CPU
    vec4 reticleUnder;  // dark underlay
};

const float kTau = 6.283185307;

const float kCoilOpenness = 0.25;
const float kStrandPerTurn = 0.12;
const float kStrandFloorPx = 1.0;

float chAbs(float x) { return abs(x); }
// @include chain_math.inl

vec4 over(vec4 acc, vec4 top) { return top + acc * (1.0 - top.a); }

float stripe(float d, float w, float feather) {
    return 1.0 - smoothstep(w - feather, w + feather, d);
}

// Preserve the original continuous green rings and their full coverage.
vec4 solidReticle(vec4 acc, float r, float outer, float feather) {
    float dOuter = abs(r - outer);
    float dInner = abs(r - outer * reticleShape.x);
    float covU = stripe(dOuter, reticleShape.z, feather) * reticleUnder.a;
    float covO = stripe(dOuter, reticleShape.y, feather) * reticleColor.a;
    float covI = stripe(dInner, reticleShape.y * 0.78, feather) * reticleColor.a;
    acc = over(acc, vec4(reticleUnder.rgb * covU, covU));
    acc = over(acc, vec4(reticleColor.rgb * covO, covO));
    return over(acc, vec4(reticleColor.rgb * covI, covI));
}

// Signed normal distance lets fine threads weave within each main strand.
// k (1 near the hand) shrinks edges and feathers with the strand; decor 0 keeps only core and edge.
vec4 energyStrand(float v, float endSq, float theta, float detail,
                  float taper, float shade, vec4 tint, float w, float decor) {
    float k = w / max(coil.w, 0.001);
    float coreFeather = 0.70 * max(k, 0.72);
    float threadFeather = 0.65 * max(k, 0.77);
    float beat = 0.5 + 0.5 * sin(theta + 2.0 * coil.z);
    float pulse = beat * beat * beat;
    float weave = sin(3.0 * theta + coil.z);
    float drift = detail * taper * w;
    float d = sqrt(v * v + endSq);
    float coreDistance = v - 0.32 * drift * weave;
    float threadA = v - drift * (1.55 + 0.40 * sin(2.0 * theta - coil.z));
    float threadB = v + drift * (1.35 + 0.35 * weave);
    float coreR = sqrt(coreDistance * coreDistance + endSq);
    float threadAR = sqrt(threadA * threadA + endSq);
    float threadBR = sqrt(threadB * threadB + endSq);
    float core = stripe(coreR, w * 0.42, coreFeather);
    float fineA = stripe(threadAR, w * 0.17, threadFeather);
    float fineB = stripe(threadBR, w * 0.13, threadFeather);
    // A narrow jade edge keeps luminous threads legible on pale rock and sky.
    float edgeCore = stripe(coreR, w * 0.42 + 0.90 * k, threadFeather);
    float edgeA = stripe(threadAR, w * 0.17 + 0.65 * k, threadFeather);
    float edgeB = stripe(threadBR, w * 0.13 + 0.65 * k, threadFeather);
    float backing = max(edgeCore * 0.72, max(edgeA, edgeB) * detail * 0.48 * decor);
    float aura = (1.0 - smoothstep(w * 0.5, w * 4.5 + k, d)) * (0.16 + 0.08 * pulse) * decor;
    float ribbon = stripe(d, w * 1.30, 1.2 * max(k, 0.5)) * (0.19 + 0.07 * weave) * decor;
    vec3 mint = mix(tint.rgb, vec3(0.85, 1.0, 0.91), 0.78);
    // Keep the invalid preview red; avoid mixing a mint highlight into red feedback.
    mint = mix(mint, mix(tint.rgb, vec3(1.0, 0.78, 0.64), 0.5),
               step(tint.g, tint.r));
    vec4 layer = vec4(tint.rgb * aura, aura);
    layer = over(layer, vec4(tint.rgb * 0.07 * backing, backing));
    layer = over(layer, vec4(tint.rgb * ribbon, ribbon));
    float threads = max(fineA * 0.82, fineB * 0.65) * detail * decor;
    layer = over(layer, vec4(mix(tint.rgb, mint, 0.4) * threads, threads));
    float bright = core * (0.93 + 0.07 * pulse);
    layer = over(layer, vec4(mix(tint.rgb, mint, mix(0.35, 1.0, decor)) * bright, bright));
    layer.rgb *= shade;
    return layer * tint.a;
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

            float metres = u * farPoint.w;
            float taper = chHandTaper(metres);
            float wave = scale * amplitude * (c * acrossA + s * acrossB);
            float offset = taper * wave;

            float dOffsetDTheta = scale * amplitude * (-s * acrossA + c * acrossB);
            float dMetresDAlong = farPoint.w * ((z * z) / (z0 * z1)) / axisLen;
            float slope = taper * dOffsetDTheta * dThetaDAlong +
                          chHandTaperSlope(metres) * dMetresDAlong * wave;
            float shrink = inversesqrt(1.0 + slope * slope);

            float overshoot = max(0.0, max(-along, along - axisLen));
            float overshootSq = overshoot * overshoot;
            float pad = chEnergyPadding(coil.w, feather);
            // Keep expensive filament detail inside the coil's envelope.
            if (abs(across) < scale * swing + pad && overshoot < pad) {
                float vA = (across - offset) * shrink;
                float vB = (across + offset) * shrink;
                float dS = sqrt(across * across + overshootSq);
                float detail = smoothstep(12.0, 34.0, turnPx);
                float resolved = smoothstep(1.5, 4.0, turnPx);
                float strandW = min(coil.w, max(kStrandFloorPx, kStrandPerTurn * turnPx));
                // Glow, ribbon, threads and motes fade out 30-50% of the way to the tip; cores and edges stay.
                float decor = 1.0 - smoothstep(0.30, 0.50, t);
                float covS = stripe(dS, pxScale.z, feather) * spineColor.a;
                covS *= max(decor, 1.0 - resolved);
                covS *= 0.55 + 0.45 * pow(0.5 + 0.5 * sin(theta + 2.0 * coil.z), 3.0);
                acc = over(acc, vec4(spineColor.rgb * covS, covS));

                float lean = clamp(radialZ, -1.0, 1.0);
                vec4 layerA = energyStrand(vA, overshootSq, theta, detail, taper,
                    mix(0.68, 1.0, 0.5 + 0.5 * lean), coreA, strandW, decor) * resolved;
                vec4 layerB = energyStrand(vB, overshootSq, theta + 3.141592654, detail, taper,
                    mix(0.68, 1.0, 0.5 - 0.5 * lean), coreB, strandW, decor) * resolved;
                // Continuous crossing order avoids a tiny color pop at the hand/phase wrap.
                vec4 strands = mix(over(layerA, layerB), over(layerB, layerA),
                                   smoothstep(-0.12, 0.12, radialZ));
                acc = over(acc, strands);

                // One procedural mote per turn. No particle buffers or extra draw calls.
                float cell = metres / max(coil.y, 0.01);
                float seed = fract(floor(cell) * 0.618033989 + 0.17);
                float travel = 0.5 + 0.22 * sin(coil.z + seed * kTau);
                float dx = (fract(cell) - travel) * turnPx;
                float dy = mix(vA, vB, step(0.5, seed)) -
                    taper * strandW * (2.6 + sin(coil.z + seed * kTau));
                float moteD = sqrt(dx * dx + dy * dy + overshootSq);
                float mote = (stripe(moteD, 0.65, 0.7) * 0.78 +
                    (1.0 - smoothstep(0.6, 3.8, moteD)) * 0.16) * detail * taper * decor;
                vec3 moteColor = mix(coreA.rgb, vec3(0.88, 1.0, 0.9), 0.7);
                moteColor = mix(moteColor, coreA.rgb, step(coreA.g, coreA.r));
                acc = over(acc, vec4(moteColor * mote, mote));
                float vis = (depthOk && z > sceneZ + 0.05) ? flags.w : 1.0;
                acc *= vis;
            }
        }

        if (flags.z > 0.5) {
            vec2 delta = texel - reticle.xy;
            float r = length(delta);
            float outer = reticle.w;
            if (reticleColor.g > reticleColor.r) {
                acc = solidReticle(acc, r, outer, feather);
            } else if (r < outer + chReticlePadding(reticleShape.y, feather)) {
                float angle = atan(delta.y, delta.x + 0.00001);
                float wave = sin(3.0 * angle + coil.z);
                float dOuter = abs(r - outer);
                float dInner = abs(r - outer * (reticleShape.x + 0.075 * wave));
                float cut = smoothstep(0.70, 0.93, cos(3.0 * angle));
                float halo = (1.0 - smoothstep(reticleShape.y,
                    reticleShape.y * 3.5 + 2.0, dOuter)) * 0.18;
                float covU = stripe(dOuter, reticleShape.z, feather) * reticleUnder.a * 0.90;
                float covO = stripe(dOuter, reticleShape.y * 0.72, 0.75) * (1.0 - 0.68 * cut);
                float innerBeat = 0.75 + 0.25 * sin(3.0 * angle + coil.z + 1.2);
                float covI = stripe(dInner, reticleShape.y * 0.50, 0.65) *
                    innerBeat;
                float seedRadius = max(0.55, outer * 0.075);
                float innerUnder = stripe(dInner, reticleShape.y * 0.50 + 0.85, 0.65) * innerBeat;
                float seedUnder = stripe(r, seedRadius + 0.7, 0.65);
                float innerBacking = max(innerUnder, seedUnder) * reticleUnder.a * 0.85;
                float seed = stripe(r, seedRadius, 0.65);
                vec3 lightColor = mix(reticleColor.rgb, vec3(0.48, 1.0, 0.40), 0.55);
                lightColor = mix(lightColor, mix(reticleColor.rgb, vec3(1.0, 0.78, 0.64), 0.4),
                    step(reticleColor.g, reticleColor.r));
                acc = over(acc, vec4(reticleColor.rgb * halo, halo) * reticleColor.a);
                acc = over(acc, vec4(reticleUnder.rgb * covU, covU));
                acc = over(acc, vec4(reticleUnder.rgb * innerBacking, innerBacking));
                acc = over(acc, vec4(lightColor * covO, covO) * reticleColor.a);
                acc = over(acc, vec4(reticleColor.rgb * covI, covI) * reticleColor.a);
                acc = over(acc, vec4(lightColor * seed, seed) * reticleColor.a);
            }
        }

        if (acc.a < 0.002 && max(acc.r, max(acc.g, acc.b)) < 0.002) {
            discard;                              // the cheap majority path
        } else {
            outColor = acc;                       // premultiplied; blend is ONE / ONE_MINUS_SRC_ALPHA
        }
    }
}
