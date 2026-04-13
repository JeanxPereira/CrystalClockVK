#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

in vec3 fragPosition;
in vec3 tanFragPosition;
in vec3 fragNormal;
in mat3 TBN;

struct Material {
    vec3  ambient;
    vec3  diffuse;
    vec3  specular;
    float shininess;
};

struct DirLight {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct PointLight {

    vec3 position;  
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
	
    float constant;
    float linear;
    float quadratic;
};

uniform sampler2D texture0;
uniform sampler2D bgTexture;
uniform vec2 resolution;
uniform sampler2D normalMap;

uniform vec3 lightColor;
uniform vec3 viewPos;
uniform vec4 colDiffuse;
uniform vec3 prismColor;

// Multi-pass pipeline uniforms (OSDSYS GS mapping)
uniform int   renderPass;
uniform float rodAlpha;

out vec4 finalColor;

vec3 CalcRimLight(vec3 viewDir, vec3 N);

vec3 CalcRimLight(vec3 viewDir, vec3 N);
vec3 CalcReflection(vec3 N);
vec3 CalcRefraction(vec3 N);

void main()
{
    vec3 norm = texture(normalMap, fragTexCoord).rgb;
	norm = norm * 2.0 - 1.0;
	norm = normalize(TBN * norm);
	
	vec3 tanViewDir = normalize((TBN * viewPos) - tanFragPosition);
    vec3 viewDir    = normalize(viewPos - fragPosition);
    vec2 screenUV = gl_FragCoord.xy / resolution;
    
    if (renderPass == 0) {
        // ═══════════════════════════════════════════════════
        // PASS 1: Base geometry (Glass Refraction)
        // OSDSYS: Distorts framebuffer background based on normal
        // ═══════════════════════════════════════════════════
        // Create PS2-like heavy distortion by bending UV towards X/Y normals
        vec2 distortedUV = screenUV + (norm.xy * 0.05); // Configurable distortion index
        
        // Sample the background tunnel texture (Y is flipped in OpenGL FBOs usually)
        vec3 bgSample = texture(bgTexture, vec2(distortedUV.x, 1.0 - distortedUV.y)).rgb;
        
        // Add subtle diffuse tint from the active time color
        vec3 tint = mix(bgSample, prismColor, 0.4);
        
        finalColor = vec4(tint, rodAlpha);
    }
    else if (renderPass <= 2) {
        // ═══════════════════════════════════════════════════
        // PASS 2-3: Colored/lit rods (Highlight Specular)
        // OSDSYS: Additive pass for edges/sharp lighting
        // ═══════════════════════════════════════════════════
        float specular = pow(max(dot(viewDir, norm), 0.0), 32.0); // Simple specular
        vec3 rim = CalcRimLight(viewDir, fragNormal);
        
        vec3 finalGlow = (prismColor * 0.8) + vec3(specular * 0.6) + rim;
        finalColor = vec4(finalGlow * rodAlpha, rodAlpha);
    }
    else {
        // ═══════════════════════════════════════════════════
        // PASS 4-5: Selected rod highlight / fill (Slider)
        // OSDSYS: Overlay solid emissive color
        // ═══════════════════════════════════════════════════
        vec3 rim = CalcRimLight(viewDir, fragNormal);
        vec3 result = (prismColor * 1.5) + rim;
        finalColor = vec4(result * rodAlpha, rodAlpha);
    }
}

vec3 CalcReflection(vec3 N)
{
    return reflect(normalize(fragPosition - viewPos), N);
}

vec3 CalcRefraction(vec3 N)
{
	return refract(normalize(fragPosition - viewPos), N, 1.00 / 1.52);
}

vec3 CalcRimLight(vec3 viewDir, vec3 N)
{
	return vec3(1.0) * smoothstep(0.3, 0.4, pow(max(0.0, 1.0 - dot(viewDir, N)), 2));
}

