#version 460

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inDepth;
layout(location = 2) in vec4 inColour;
layout(location = 3) in vec3 inTexture;
layout(location = 4) in vec4 inStepped;
layout(location = 5) in vec2 inFirst;

layout(push_constant) uniform DrawState {
    ivec4 scissor; ivec4 blend; ivec4 misc; ivec4 tex; ivec4 texSize; ivec4 addressU; ivec4 addressV; ivec4 flags;
} state;

layout(location = 0) noperspective out vec2 outDepth;
layout(location = 1) noperspective out vec4 outColour;
layout(location = 2) noperspective out vec3 outTexture;
layout(location = 3) flat out vec4 outStepped;
layout(location = 4) flat out ivec2 outFirst;

void main() {
    // The GS samples a pixel at its integer coordinate; Vulkan samples at the centre.
    gl_Position = vec4((inPosition + 0.5) / vec2(state.flags.yz) * 2.0 - 1.0, 0.0, 1.0);
    outDepth = inDepth;
    outColour = inColour;
    outTexture = inTexture;
    outStepped = inStepped;
    outFirst = ivec2(inFirst);
}
