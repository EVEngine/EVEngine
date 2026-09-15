#version 450

// Box-projected screen-space decal. The G-buffer hardware depth reconstructs
// world position; fragments outside the unit decal box are rejected.
//
// Projection modes (extraParams.z):
//   0 = planar  — sample local.xy (classic UE Decal; stretches on grazing faces)
//   1 = triplanar — sample YZ/XZ/XY in local space and blend by |nLocal|^sharpness
//                   so side faces keep undistorted texture (UE5 "三维投射贴花")
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
    vec4 extraParams; // x=emissive, y=blendMode, z=projectionMode, w=blendSharpness
} decal;

layout(location = 0) flat in vec4 vUV;
layout(location = 1) flat in vec4 vFade;
layout(location = 2) flat in vec4 vExtra;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outParams;

vec4 sampleAtlas(sampler2D tex, vec2 localUV) {
    vec2 atl = vUV.xy + clamp(localUV, 0.0, 1.0) * vUV.zw;
    return texture(tex, atl);
}

void main() {
    vec2 uv = gl_FragCoord.xy * cam.nearFarTexel.zw;
    float ndcZ = texture(hwDepthTex, uv).r;
    if (ndcZ <= 0.0 || ndcZ >= 1.0) discard;

    vec4 clip = vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
    vec4 wp = cam.invViewProj * clip;
    vec3 worldPos = wp.xyz / max(wp.w, 1e-6);

    mat4 invModel = inverse(decal.model);
    vec4 lp = invModel * vec4(worldPos, 1.0);
    vec3 local = lp.xyz / max(lp.w, 1e-6);
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    vec3 surfaceN = texture(gbNormalTex, uv).xyz * 2.0 - 1.0;
    vec3 decalFwd = normalize(mat3(decal.model) * vec3(0.0, 0.0, 1.0));
    bool useTriplanar = vExtra.z > 0.5;

    // Planar mode hides grazing faces (where single-axis UVs stretch). Triplanar
    // keeps side faces and only rejects true backfaces.
    float facing = dot(surfaceN, decalFwd);
    if (useTriplanar) {
        if (facing < -0.05) discard;
    } else if (facing < 0.1) {
        discard;
    }

    vec4 alb;
    vec4 nrm;
    vec4 prm;
    float edgeFade;

    if (!useTriplanar) {
        vec2 decalUV = clamp(local.xy + 0.5, 0.0, 1.0);
        alb = sampleAtlas(decalAlbedo, decalUV);
        nrm = sampleAtlas(decalNormal, decalUV);
        prm = sampleAtlas(decalParams, decalUV);
        vec2 edge = smoothstep(vec2(0.0), vec2(0.06), decalUV) *
                    smoothstep(vec2(1.0), vec2(0.94), decalUV);
        edgeFade = edge.x * edge.y;
    } else {
        // Weights from surface normal in decal-local space so actor rotation /
        // non-uniform scale still align the blend axes with the volume.
        vec3 nLocal = normalize(mat3(invModel) * surfaceN);
        float sharpness = max(vExtra.w, 1.0);
        vec3 w = pow(abs(nLocal), vec3(sharpness));
        w /= max(w.x + w.y + w.z, 1e-5);

        // Axis projections inside the unit box → [0,1] UVs.
        vec2 uvYZ = local.yz + 0.5; // along local X
        vec2 uvXZ = local.xz + 0.5; // along local Y
        vec2 uvXY = local.xy + 0.5; // along local Z

        alb = sampleAtlas(decalAlbedo, uvYZ) * w.x +
              sampleAtlas(decalAlbedo, uvXZ) * w.y +
              sampleAtlas(decalAlbedo, uvXY) * w.z;
        nrm = sampleAtlas(decalNormal, uvYZ) * w.x +
              sampleAtlas(decalNormal, uvXZ) * w.y +
              sampleAtlas(decalNormal, uvXY) * w.z;
        prm = sampleAtlas(decalParams, uvYZ) * w.x +
              sampleAtlas(decalParams, uvXZ) * w.y +
              sampleAtlas(decalParams, uvXY) * w.z;

        vec3 t = local + 0.5;
        vec3 edge = smoothstep(vec3(0.0), vec3(0.06), t) *
                    smoothstep(vec3(1.0), vec3(0.94), t);
        edgeFade = edge.x * edge.y * edge.z;
    }

    float cov = alb.a * clamp(vFade.x, 0.0, 1.0) * edgeFade;
    if (cov <= 0.001) discard;

    outAlbedo = vec4(alb.rgb, cov);
    outNormal = vec4(nrm.rgb * step(0.001, vFade.y),
                     cov * clamp(vFade.y, 0.0, 1.0));
    outParams = vec4(prm.r * clamp(vFade.z, 0.0, 1.0),
                     prm.g * clamp(vFade.w, 0.0, 1.0),
                     prm.b * clamp(vExtra.x, 0.0, 1.0), cov);
}
