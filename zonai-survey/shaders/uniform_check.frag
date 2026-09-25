// SPDX-License-Identifier: MIT
#version 450
layout(location = 0) in vec2 screenUv;
layout(location = 0) out vec4 outColor;
layout(std140, binding = 0) uniform UniformCheck { vec4 color; };
void main() {
    if (screenUv.x > 0.28 || screenUv.y > 0.22) discard;
    outColor = color;
}
