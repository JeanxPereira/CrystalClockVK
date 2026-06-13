#version 450

// GS draw: vertices arrive pre-transformed to clip space (gsvk::toClip on CPU).
layout(location = 0) in vec3 inPos;    // clip-space xy + normalized z
layout(location = 1) in vec4 inColor;  // RGBA8 vertex color (GS alpha 0..128 in a)
layout(location = 2) in vec2 inUV;     // normalized texture coords

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    gl_Position = vec4(inPos, 1.0);
    vColor = inColor;
    vUV = inUV;
}
