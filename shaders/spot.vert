#version 450

// Light-spot billboard: transform position, pass the [-1,1] glow uv + colour.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform Push {
    mat4 mvp;
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vUV = inUV;
    vColor = inColor;
}
