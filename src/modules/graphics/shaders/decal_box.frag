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

struct DecalInstanceData {
    mat4 model;
    vec4 uvRect;
    vec4 fadeParams;
    vec4 extraParams;
};
layout(set = 0, binding = 6, std140) readonly buffer DecalInstances {
    DecalInstanceData instances[];
} inst;

layout(location = 0) flat in vec4 vUV;
layout(location = 1) flat in vec4 vFade;
layout(location = 2) flat in vec4 vExtra;
layout(location = 3) flat in int vInstance;

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
    // Reconstruct the per-instance model on the CPU side is not possible in a
    // read-only pass, so inverse() is computed here (small boxes only).
    vec4 lp = inverse(inst.instances[vInstance].model) * vec4(worldPos, 1.0);
    vec3 local = lp.xyz / max(lp.w, 1e-6);
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    // Backface test: the surface must face the decal projection axis.
    vec3 surfaceN = texture(gbNormalTex, uv).xyz * 2.0 - 1.0;
    vec3 decalFwd = normalize(mat3(inst.instances[vInstance].model) * vec3(0.0, 0.0, 1.0));
    if (dot(surfaceN, decalFwd) < 0.1) discard;

    vec2 decalUV = clamp(local.xy + 0.5, 0.0, 1.0);
    vec2 atl = vUV.xy + decalUV * vUV.zw;
    vec4 alb = texture(decalAlbedo, atl);
    vec4 nrm = texture(decalNormal, atl);
    vec4 prm = texture(decalParams, atl);

    float cov = alb.a * clamp(vFade.x, 0.0, 1.0);
    // Feather the box edges so decals do not show a hard rectangle.
    vec2 edge = smoothstep(vec2(0.0), vec2(0.06), decalUV) *
                smoothstep(vec2(1.0), vec2(0.94), decalUV);
    cov *= edge.x * edge.y;
    if (cov <= 0.001) discard;

    // Non-premultiplied "over" contributions (alpha-over compositing in the
    // layer, decoded directly in mesh3d.frag); strengths damp the values.
    outAlbedo = vec4(alb.rgb, cov);
    // Normal: value is premultiplied by coverage, weight is coverage * strength.
    // step() zeroes the value when strength is 0 so additive-mode decals never
    // pollute the accumulated normal with the flat placeholder.
    outNormal = vec4(nrm.rgb * step(0.001, vFade.y), cov * clamp(vFade.y, 0.0, 1.0));
    // Params: strengths damp the stored target values; coverage rides in alpha.
    outParams = vec4(prm.r * clamp(vFade.z, 0.0, 1.0),
                     prm.g * clamp(vFade.w, 0.0, 1.0),
                     prm.b * clamp(vExtra.x, 0.0, 1.0),
                     cov);
}
