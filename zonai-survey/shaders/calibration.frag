// SPDX-License-Identifier: MIT
#version 450
layout(location = 0) in vec2 screenUv;
layout(location = 0) out vec4 outColor;

void main() {
    if (screenUv.x > 0.28 || screenUv.y > 0.22) discard;
    outColor = vec4(screenUv.x * 3.5, 0.3, screenUv.y * 4.5, 1.0);
}
