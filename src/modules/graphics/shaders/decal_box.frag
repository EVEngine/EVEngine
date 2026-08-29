#version 450

// Box-projected screen-space decal. The G-buffer hardware depth reconstructs
// world position; fragments outside the unit decal box are rejected.
layout(set = 0, binding = 0) uniform sampler2D decalAlbedo;
layout(set = 0, binding = 1) uniform sampler2D decalNormal;
layout(set = 0, binding = 2) uniform sampler2D decalParams;
layout(set = 0, binding = 3) uniform sampler2D hwDepthTex;
layout(set = 0, binding = 4) uniform sampler2D gbNormalTex;
layout(set = 0, binding = 5, std140) uniform Camera {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 nearFarTexel;
} cam;

layout(push_constant) uniform DecalPush {
    mat4 model;
    vec4 uvRect;
    vec4 fadeParams;
    vec4 extraParams;
} decal;

layout(location = 0) flat in vec4 vUV;
layout(location = 1) flat in vec4 vFade;
layout(location = 2) flat in vec4 vExtra;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outParams;

void main() {
    vec2 uv = gl_FragCoord.xy * cam.nearFarTexel.zw;
    float ndcZ = texture(hwDepthTex, uv).r;
    if (ndcZ <= 0.0 || ndcZ >= 1.0) discard;

    vec4 clip = vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    vec4 wp = cam.invViewProj * clip;
    vec3 worldPos = wp.xyz / max(wp.w, 1e-6);

    vec4 lp = inverse(decal.model) * vec4(worldPos, 1.0);
    vec3 local = lp.xyz / max(lp.w, 1e-6);
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    vec3 surfaceN = texture(gbNormalTex, uv).xyz * 2.0 - 1.0;
    vec3 decalFwd = normalize(mat3(decal.model) * vec3(0.0, 0.0, 1.0));
    if (dot(surfaceN, decalFwd) < 0.1) discard;

    vec2 decalUV = clamp(local.xy + 0.5, 0.0, 1.0);
    vec2 atl = vUV.xy + decalUV * vUV.zw;
    vec4 alb = texture(decalAlbedo, atl);
    vec4 nrm = texture(decalNormal, atl);
    vec4 prm = texture(decalParams, atl);

    float cov = alb.a * clamp(vFade.x, 0.0, 1.0);
    vec2 edge = smoothstep(vec2(0.0), vec2(0.06), decalUV) *
                smoothstep(vec2(1.0), vec2(0.94), decalUV);
    cov *= edge.x * edge.y;
    if (cov <= 0.001) discard;

    outAlbedo = vec4(alb.rgb, cov);
    outNormal = vec4(nrm.rgb * step(0.001, vFade.y),
                     cov * clamp(vFade.y, 0.0, 1.0));
    outParams = vec4(prm.r * clamp(vFade.z, 0.0, 1.0),
                     prm.g * clamp(vFade.w, 0.0, 1.0),
                     prm.b * clamp(vExtra.x, 0.0, 1.0), cov);
}
