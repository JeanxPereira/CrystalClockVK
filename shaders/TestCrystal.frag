#version 450

// 1:1 port of Wallpaper Engine crystal.frag (scene 1979606285) with every
// hardcoded constant lifted into FrameUBO tuning fields.
// WE reference math:
//   offset = refract(viewDir, normal, 0.5).xy / screenPos.w
//   refract = tex(_rt_Reflection, screenUV + offset) * 2 * (0.75 + emissive*4)
//   final = mix(refract, diffuse, diffuse.r*0.2) * mix(color1, color2, tintLerp) + reflect

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragScreenPos;
layout(location = 4) in vec3 fragViewDir;
layout(location = 5) in float fragHeight;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 view;
    vec4 viewPos;
    vec4 prismColor;
    vec4 refractA; // x=eta, y=refractScale, z=refractBoost, w=rimStrength
    vec4 refractB; // x=emissiveBase, y=diffuseMix, z=reflectStrength, w=fadeAlpha
    vec4 tintA;    // rgb=color1, w=tintLerp override (<0 = animate)
    vec4 tintB;    // rgb=color2, w=colorPeriod
    vec4 lightDir[3];
    vec4 lightColor[3];
    vec4 ambient;  // rgb=ambient, w=icon lighting enable
} ubo;

layout(set = 0, binding = 1) uniform sampler2D bgTexture;
layout(set = 0, binding = 2) uniform sampler2D albedoTexture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;   // w=composition mode (0=front opaque, 1=back scene mix)
    vec4 screenParams; // z=time
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(fragViewDir);
    vec2 screenUV = (fragScreenPos.xy / fragScreenPos.z) * 0.5 + 0.5;

    vec4 diffuse = texture(albedoTexture, fragUV * 4.0);

    float rim = 1.0 - max(0.0, dot(V, N));
    float emissive = rim * ubo.refractA.w + ubo.refractB.x;

    vec2 refrOffset = refract(V, N, ubo.refractA.x).xy / fragScreenPos.z * ubo.refractA.y;
    vec3 refr = texture(bgTexture, screenUV + refrOffset).rgb;
    refr *= (0.75 + emissive * ubo.refractA.z);

    float refl = texture(albedoTexture, N.xy + vec2(fragHeight * 0.002)).r;
    refl = refl * refl;
    refl = refl * refl;
    refl = refl * refl * ubo.refractB.z;

    vec3 finalColor = mix(refr, diffuse.rgb, diffuse.r * ubo.refractB.y);

    if (ubo.ambient.w > 0.5) {
        // OSDSYS icon rig (hddosd.elf 0x2BD7B0): lit = ambient + sum(Ci * max(0, N.-Di))
        vec3 lit = ubo.ambient.rgb;
        for (int i = 0; i < 3; i++) {
            lit += ubo.lightColor[i].rgb * max(0.0, dot(N, normalize(-ubo.lightDir[i].xyz)));
        }
        finalColor *= lit;
    } else {
        float time = pc.screenParams.z;
        float tintLerp = ubo.tintA.w < 0.0
            ? abs(mod(time / max(ubo.tintB.w, 0.001), 1.0) * 2.0 - 1.0)
            : ubo.tintA.w;
        finalColor *= mix(ubo.tintA.rgb, ubo.tintB.rgb, tintLerp);
    }
    finalColor += refl;

    float fade = clamp(ubo.refractB.w, 0.0, 1.0);
    if (pc.rodColor.w > 0.5) {
        vec3 sceneColor = texture(bgTexture, screenUV).rgb;
        outColor = vec4(mix(sceneColor, finalColor, fade), 1.0);
    } else {
        outColor = vec4(finalColor * fade, 1.0);
    }
}
