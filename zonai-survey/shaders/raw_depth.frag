// SPDX-License-Identifier: MIT
#version 450
layout(location = 0) in vec2 screenUv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D sceneDepth;
void main() {
    float d = texture(sceneDepth, vec2(screenUv.x, 1.0 - screenUv.y)).r;
    outColor = isnan(d) || isinf(d) ? vec4(1, 0, 1, 1) : vec4(vec3(d), 1);
}
