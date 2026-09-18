// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Fullscreen triangle from gl_VertexID alone; the chain is computed in the fragment stage.
#version 450
layout(location = 0) out vec2 screenUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenUv = p;                                 // [0,2] on both axes
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
