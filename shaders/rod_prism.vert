#version 450

// 3D crystal prism rod: transform position + normal, pass world normal and a
// view-space depth cue to the fragment stage for the glassy facet look.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform Push {
    mat4 mvp;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorldPos;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = normalize(inNormal);
    vColor = inColor;
    vWorldPos = inPos;
}
