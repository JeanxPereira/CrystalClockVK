#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform TestBgPush {
    vec4 color1;
    vec4 color2;
    vec4 params; // x=mode (0=vstripes,1=hstripes,2=checker,3=grid), y=scale, z=aspect, w=scroll
} pc;

void main() {
    float mode = pc.params.x;
    float scale = pc.params.y;
    vec2 uv = fragUV * vec2(pc.params.z, 1.0) * scale + pc.params.w;

    float t;
    if (mode < 0.5) {
        t = step(0.5, fract(uv.x));
    } else if (mode < 1.5) {
        t = step(0.5, fract(uv.y));
    } else if (mode < 2.5) {
        t = mod(floor(uv.x) + floor(uv.y), 2.0);
    } else {
        vec2 g = abs(fract(uv) - 0.5);
        t = step(0.45, max(g.x, g.y));
    }

    outColor = vec4(mix(pc.color1.rgb, pc.color2.rgb, t), 1.0);
}
