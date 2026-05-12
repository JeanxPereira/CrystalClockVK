#version 450

// Tunnel fragment shader — ported 1:1 from Raylib's tunnel.fs
// Point light attenuation with scrolling noise texture.

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragPosition;
layout(location = 2) in vec3 fragNormal;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    vec4 viewPos;
    vec4 prismColor;   // a = time
} ubo;

layout(set = 0, binding = 1) uniform sampler2D noiseTexture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;
    vec4 screenParams;  // z = time
} pc;

layout(location = 0) out vec4 outColor;

// Raylib's exact tunnel colors
const vec3 mainColor = vec3(0.28, 0.19, 0.43);
const vec3 secondaryColor = vec3(0.18, 0.10, 0.32);

// Raylib's exact light constants
const float LIGHT_KC = 1.0;
const float LIGHT_KL = 0.007;
const float LIGHT_KQ = 0.0002;
const vec3 LIGHT_AMBIENT = vec3(0.1, 0.1, 0.1);
const vec3 LIGHT_DIFFUSE = vec3(1.0, 1.0, 1.0);

vec3 sampleNoise() {
    float time = pc.screenParams.z;
    vec2 texCoord = fragTexCoord + vec2(time, 0.0);
    float noise = texture(noiseTexture, texCoord).r;
    float t = smoothstep(0.0, 1.0, noise);
    return mix(mainColor * 1.25, secondaryColor, t);
}

vec3 calcPointLight(vec3 normal, vec3 fragPos, vec3 viewDir) {
    vec3 lightPos = ubo.viewPos.xyz;
    vec3 lightDir = normalize(lightPos - fragPos);

    // Flip normal if facing away from light (inside cylinder)
    if (dot(normal, lightDir) < 0.0)
        normal = -normal;

    float diff = max(dot(normal, lightDir), 0.0);
    float distance = length(lightPos - fragPos);
    float attenuation = 1.0 / (LIGHT_KC + LIGHT_KL * distance + LIGHT_KQ * distance * distance);

    vec3 noise = sampleNoise();
    vec3 ambient = LIGHT_AMBIENT * noise;
    vec3 diffuse = LIGHT_DIFFUSE * diff * noise;

    ambient *= attenuation;
    diffuse *= attenuation;

    return ambient + diffuse;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 fragVec = ubo.viewPos.xyz - fragPosition;
    float fade = 1.0 - smoothstep(0.0, 100.0, length(fragVec));

    vec3 color = calcPointLight(N, fragPosition, normalize(fragVec));
    outColor = vec4(color * fade, 1.0);
}
