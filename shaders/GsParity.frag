#version 460
#extension GL_ARB_fragment_shader_interlock : require

layout(pixel_interlock_ordered) in;

layout(set = 0, binding = 0, rgba8ui) uniform uimage2D targetImage;
layout(set = 0, binding = 1, r32ui) uniform uimage2D depthImage;
layout(set = 0, binding = 2) uniform usampler2D textureImage;

// scissor: x0 y0 x1 y1 (inclusive)        blend: a b c d (terms 0 source 1 destination 2 zero; c 0 source alpha 1 destination alpha 2 fixed)
// misc: blend on, fixed, depth test (0 never 1 always 2 >= 3 >), depth write
// tex: on, coordinates (0 texel 1 projective 2 sprite steps), filter (0 nearest 1 bilinear), alpha (0 texel 1 constant)
// texSize: width, height, constant alpha, zero when black      address: mode (0 repeat 1 clamp 2 region clamp 3 region repeat), min, max
// flags: antialias, target width, target height
layout(push_constant) uniform DrawState {
    ivec4 scissor; ivec4 blend; ivec4 misc; ivec4 tex; ivec4 texSize; ivec4 addressU; ivec4 addressV; ivec4 flags;
} state;

layout(location = 0) noperspective in vec2 inDepth;
layout(location = 1) noperspective in vec4 inColour;
layout(location = 2) noperspective in vec3 inTexture;
layout(location = 3) flat in vec4 inStepped;
layout(location = 4) flat in ivec2 inFirst;

int address(int value, ivec4 mode, int size) {
    if (mode.x == 0) return value & (size - 1);
    if (mode.x == 1) return clamp(value, 0, size - 1);
    if (mode.x == 2) return clamp(value, mode.y, mode.z);
    return (value & mode.y) | mode.z;
}

ivec4 texel(ivec2 at) {
    ivec2 p = ivec2(address(at.x, state.addressU, state.texSize.x), address(at.y, state.addressV, state.texSize.y));
    ivec4 c = ivec4(texelFetch(textureImage, clamp(p, ivec2(0), textureSize(textureImage, 0) - 1), 0));
    if (state.tex.w == 1) c.a = (state.texSize.w == 1 && c.rgb == ivec3(0)) ? 0 : state.texSize.z;
    return c;
}

// Bilinear weights are four bits: a + ((b - a) * f >> 4).
ivec4 mix4(ivec4 a, ivec4 b, int f) { return a + (((b - a) * f) >> 4); }

// A sprite's coordinate as the GS rasterizer (SSE4.1 build, four-pixel blocks) steps it: V adds its step once per row in
// floats; U is the row's start, truncated, plus the truncated step times the lane offset inside a block, plus whole blocks.
ivec2 steppedCoordinate(ivec2 pixel) {
    precise float v = inStepped.y;
    for (int row = inFirst.y; row < pixel.y; row++) v += inStepped.w;
    int skip = inFirst.x & 3;
    int offset = pixel.x - (inFirst.x - skip);
    precise float lane = inStepped.z * float((offset & 3) - skip);
    precise float block = inStepped.z * 4.0;
    return ivec2(int(inStepped.x) + int(lane) + (offset >> 2) * int(block), int(v));
}

ivec4 sampleTexture(ivec2 pixel) {
    ivec2 uv;
    if (state.tex.y == 2) {
        uv = steppedCoordinate(pixel);
    } else {
        vec2 texels = state.tex.y == 0 ? inTexture.xy : inTexture.xy / inTexture.z * vec2(state.texSize.xy);
        uv = ivec2(floor(texels * 65536.0));
        if (state.tex.z == 1) uv -= 0x8000;
    }
    if (state.tex.z == 0) return texel(uv >> 16);
    ivec2 f = (uv >> 12) & 0xf;
    ivec2 i = uv >> 16;
    ivec4 top = mix4(texel(i), texel(i + ivec2(1, 0)), f.x);
    ivec4 bottom = mix4(texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)), f.x);
    return mix4(top, bottom, f.y);
}

ivec3 term(int which, ivec3 source, ivec3 destination) { return which == 0 ? source : which == 1 ? destination : ivec3(0); }

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (pixel.x < state.scissor.x || pixel.y < state.scissor.y || pixel.x > state.scissor.z || pixel.y > state.scissor.w) discard;

    ivec4 colour = ivec4(inColour);
    if (state.tex.x == 1) colour = min((sampleTexture(pixel) * colour) >> 7, ivec4(255));
    if (state.flags.x == 1) colour.a = 0x80;
    uint depth = uint(inDepth.x * 4096.0 + inDepth.y);

    beginInvocationInterlockARB();
    uint stored = imageLoad(depthImage, pixel).r;
    bool visible = state.misc.z == 1 || (state.misc.z == 2 && depth >= stored) || (state.misc.z == 3 && depth > stored);
    if (visible) {
        ivec4 result = colour;
        if (state.misc.x == 1) {
            ivec4 destination = ivec4(imageLoad(targetImage, pixel));
            int factor = state.blend.z == 0 ? colour.a : state.blend.z == 1 ? destination.a : state.misc.y;
            ivec3 a = term(state.blend.x, colour.rgb, destination.rgb);
            ivec3 b = term(state.blend.y, colour.rgb, destination.rgb);
            ivec3 d = term(state.blend.w, colour.rgb, destination.rgb);
            result.rgb = clamp((((a - b) * factor) >> 7) + d, ivec3(0), ivec3(255));
        }
        imageStore(targetImage, pixel, uvec4(result));
        if (state.misc.w == 1) imageStore(depthImage, pixel, uvec4(depth, 0u, 0u, 0u));
    }
    endInvocationInterlockARB();
}
