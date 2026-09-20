#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;
layout(location=0) out vec4 outColor;
void main() {
    vec2 p = vWorldPos.xz;
    float wx = sin(p.x * 1.7 + p.y * 0.43) + 0.55 * sin(p.x * 4.1 - p.y * 1.3);
    float wz = cos(p.y * 1.9 - p.x * 0.37) + 0.55 * cos(p.y * 3.7 + p.x * 1.1);
    vec3 N = normalize(vec3(wx * 0.055, 1.0, wz * 0.055));
    vec3 V = normalize(vCameraPos - vWorldPos);
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float ndv = max(dot(N, V), 0.0);
    float ndl = max(dot(N, L), 0.0);
    float fresnel = 0.035 + 0.50 * pow(1.0 - ndv, 4.0);
    float glint = pow(max(dot(N, H), 0.0), 150.0) * 1.15;
    vec3 deep = vec3(0.018, 0.16, 0.205);
    vec3 shallow = vec3(0.045, 0.36, 0.39);
    vec3 water = mix(deep, shallow, 0.35 + 0.25 * N.y);
    water *= mix(vec3(1.0), max(vTint.rgb, vec3(0.12)), 0.18);
    vec3 sky = ubo.ambient.rgb * mix(vec3(0.55, 0.72, 0.82), vec3(1.0), fresnel);
    vec3 color = water * (0.72 + 0.58 * ndl) + sky * fresnel +
                 ubo.lightColor.rgb * glint;
    color = color / (color + vec3(0.68));
    float nearZ = max(ubo.clipInfo.x, 1e-4);
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float viewDepth = max(-vViewPos.z, 0.0);
    float linearDepth = clamp((viewDepth - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outColor = vec4(color, linearDepth);
}
