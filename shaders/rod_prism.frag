#version 450

// Crystal-bar look for the generated dial rods (approximation of the GS crystal
// pass, driven by geometry not a dump): a translucent tinted body, per-facet
// diffuse shading so the box faces read as 3D, and a bright Fresnel-style rim
// where facets turn away from the camera (the glass edge glow).
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Camera looks down -Z at the face-on dial (world +Z toward viewer).
    const vec3 viewDir = vec3(0.0, 0.0, 1.0);
    const vec3 lightDir = normalize(vec3(-0.3, 0.5, 0.8));

    vec3 n = normalize(vNormal);
    float diff = 0.65 + 0.35 * max(dot(n, lightDir), 0.0);   // soft diffuse
    float fres = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);  // rim / edge glow

    // Body keeps the rod's tint (readable AM blue / PM red); the crystal edge is
    // a restrained whitish highlight only on facets turned away from the camera.
    vec3 body = vColor.rgb * diff;
    vec3 edge = mix(vColor.rgb, vec3(1.0), 0.6) * fres * 0.5;
    outColor = vec4(body + edge, 1.0);
}
