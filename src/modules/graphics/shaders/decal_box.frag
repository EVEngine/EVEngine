#version 450

// Box-projected screen-space decal:
//   - reads the G-buffer hwDepth (binding 3) to reconstruct the world position
//   - rejects fragments outside the unit decal box (z thickness culling)
//   - rejects surfaces facing away from the decal +Z axis
//   - writes premultiplied albedo / normal / params into the DecalLayer

layout(set = 0, binding = 0) uniform sampler2D decalAlbedo;
layout(set = 0, binding = 1) uniform sampler2D decalNormal;
layout(set = 0, binding = 2) uniform sampler2D decalParams;
layout(set = 0, binding = 3) uniform sampler2D hwDepthTex;  // D32, .r = Vulkan NDC z
layout(set = 0, binding = 4) uniform sampler2D gbNormalTex; // world normal * 0.5 + 0.5
layout(set = 0, binding = 5, std140) uniform Camera {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 nearFarTexel; // x = near, y = far, z = 1/width, w = 1/height
} cam;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 uvRect;    // atlas region [x, y, w, h]
    vec4 fadeParams;   // x = fade, y = normalStrength, z = roughStrength, w = metalStrength
    vec4 extraParams;  // x = emissiveStrength, y = blendMode, z/w unused
} pc;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outParams;

void main() {
    vec2 uv = gl_FragCoord.xy * cam.nearFarTexel.zw;
    float ndcZ = texture(hwDepthTex, uv).r;
    if (ndcZ <= 0.0 || ndcZ >= 1.0) discard;  // sky / no depth

    // Reconstruct world position (same math as ao_from_depth.frag).
    float nearZ = max(cam.nearFarTexel.x, 1e-4);
    float farZ = max(cam.nearFarTexel.y, nearZ + 1e-3);
    float viewZ = (nearZ * farZ) / max(farZ - ndcZ * (farZ - nearZ), 1e-6);
    vec4 clip = vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    vec4 wp = cam.invViewProj * clip;
    vec3 worldPos = wp.xyz / max(wp.w, 1e-6);

    // Decal local space: unit box [-0.5, 0.5]^3, +Z = decal forward.
    vec4 lp = inverse(pc.model) * vec4(worldPos, 1.0);
    vec3 local = lp.xyz / max(lp.w, 1e-6);
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    // Backface test: the surface must face the decal projection axis.
    vec3 surfaceN = texture(gbNormalTex, uv).xyz * 2.0 - 1.0;
    vec3 decalFwd = normalize(mat3(pc.model) * vec3(0.0, 0.0, 1.0));
    if (dot(surfaceN, decalFwd) < 0.1) discard;

    vec2 decalUV = clamp(local.xy + 0.5, 0.0, 1.0);
    vec2 atl = pc.uvRect.xy + decalUV * pc.uvRect.zw;
    vec4 alb = texture(decalAlbedo, atl);
    vec4 nrm = texture(decalNormal, atl);
    vec4 prm = texture(decalParams, atl);

    float cov = alb.a * clamp(pc.fadeParams.x, 0.0, 1.0);
    // Feather the box edges so decals do not show a hard rectangle.
    vec2 edge = smoothstep(vec2(0.0), vec2(0.06), decalUV) *
                smoothstep(vec2(1.0), vec2(0.94), decalUV);
    cov *= edge.x * edge.y;
    if (cov <= 0.001) discard;

    // Premultiplied contributions; per-channel strengths baked per decal.
    outAlbedo = vec4(alb.rgb * cov, cov);
    outNormal = vec4(nrm.rgb * cov * clamp(pc.fadeParams.y, 0.0, 1.0), cov);
    outParams = vec4(prm.r * cov * clamp(pc.fadeParams.z, 0.0, 1.0),
                     prm.g * cov * clamp(pc.fadeParams.w, 0.0, 1.0),
                     prm.b * cov * clamp(pc.extraParams.x, 0.0, 1.0),
                     cov);
}
