#version 450
#extension GL_GOOGLE_include_directive : enable
#include "anime_shadow.glsl"

// Independent YSA-style main-light material. Parameter order is owned by
// kYsaParams in StyleShaders.cpp; the material parameter colors are authored in
// sRGB, while the mesh tint is an engine color value and therefore already
// linear (asset/runtime pack spec: color values are linear floats, color
// textures declare sRGB).
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;
layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(push_constant) uniform Params { float data[32]; } u;
layout(location = 0) out vec4 outColor;

float ramp(float midpoint, float width, float value) {
    return smoothstep(midpoint, midpoint + max(width, 0.00001), value);
}

vec3 toLinear(vec3 color) {
    color = max(color, vec3(0.0));
    return mix(pow((color + 0.055) / 1.055, vec3(2.4)), color / 12.92,
               lessThanEqual(color, vec3(0.04045)));
}

void main() {
    vec4 albedoColor = texture(albedo, vUV);
    vec4 base = albedoColor * vTint;
    if (base.a < u.data[20]) discard;
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vLightDir);
    vec3 V = normalize(vCameraPos - vWorldPos);
    float diffuse = ramp(u.data[0], u.data[1], dot(N, L));
    float visibility = animeShadow(vWorldPos, N);
    float shadow = max(ramp(0.0, u.data[6], visibility), 1.0 - u.data[5]);
    float light = diffuse * shadow;
    vec3 ambient = vec3(u.data[2], u.data[3], u.data[4]);
    vec3 specularColor = vec3(u.data[10], u.data[11], u.data[12]);
    vec3 rimColor = vec3(u.data[17], u.data[18], u.data[19]);
    // EVEngine albedo textures use UNORM sampling; presentation encodes sRGB.
    // Match the reference project's selected lighting space explicitly.
    if (u.data[22] > 0.5) {
        // Decode the sRGB-encoded albedo sample on its own: the tint it meets is
        // already linear, so decoding their product would apply one transfer
        // function to a mixed-space value.
        base.rgb = toLinear(albedoColor.rgb) * vTint.rgb;
        ambient = toLinear(ambient);
        specularColor = toLinear(specularColor);
        rimColor = toLinear(rimColor);
    }
    vec3 color = base.rgb * mix(ambient, vLightColor, light);

    vec3 halfSum = V + L;
    vec3 H = halfSum * inversesqrt(max(dot(halfSum, halfSum), 0.000001));
    float specular = ramp(u.data[8], u.data[9], dot(N, H));
    vec3 highlight = specular * u.data[7] * specularColor;
    highlight *= vLightColor * light;

    float factor = mix(1.0, dot(L, N), u.data[16]);
    float midpoint = mix(1.0, u.data[14], factor);
    float rim = ramp(midpoint, u.data[15], 1.0 - dot(V, N));
    vec3 edge = rim * u.data[13] * rimColor;
    edge *= mix(1.0, light, u.data[21]);
    // The source graph combines these lobes with Maximum, not addition.
    color += max(highlight, edge);
    outColor = vec4(u.data[22] > 0.5 ? color : toLinear(color), base.a);
}
