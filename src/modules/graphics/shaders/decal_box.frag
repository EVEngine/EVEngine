#version 450

// Box-projected screen-space decal. The G-buffer hardware depth reconstructs
// world position; fragments outside the unit decal box are rejected.
//
// Projection modes (extraParams.z):
//   0 = planar  — sample local.xy (classic UE Decal; stretches on grazing faces)
//   1 = triplanar — sample YZ/XZ/XY in local space and blend by |nLocal|^sharpness
//                   so side faces keep undistorted texture (UE5 "三维投射贴花")
//   2 = spherical — longitude/latitude mapping around the projector center
//   3 = world-aligned — repeating triplanar UVs anchored in world coordinates
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
    vec4 surfaceParams; // x=POM scale, y=min layers, z=max layers, w=edge fade width
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

float sampleHeight(vec2 localUV) {
    vec2 atl = vUV.xy + clamp(localUV, 0.0, 1.0) * vUV.zw;
    return textureLod(decalParams, atl, 0.0).a;
}

vec2 parallaxUV(vec2 baseUV, vec3 viewTS) {
    float scale = decal.surfaceParams.x;
    if (scale <= 0.0) return baseUV;
    float minLayers = clamp(decal.surfaceParams.y, 1.0, 64.0);
    float maxLayers = clamp(decal.surfaceParams.z, minLayers, 64.0);
    float layers = mix(maxLayers, minLayers, clamp(abs(viewTS.z), 0.0, 1.0));
    float layerDepth = 1.0 / layers;
    vec2 delta = (viewTS.xy / max(abs(viewTS.z), 0.08)) * scale / layers;
    vec2 currentUV = baseUV;
    float currentDepth = 0.0;
    for (int index = 0; index < 64; ++index) {
        float surfaceDepth = 1.0 - sampleHeight(currentUV);
        if (currentDepth >= surfaceDepth || float(index) >= layers) break;
        currentUV -= delta;
        currentDepth += layerDepth;
    }
    return currentUV;
}

float edgeMask(vec2 coordinates) {
    float width = clamp(decal.surfaceParams.w, 0.0, 0.49);
    if (width <= 0.0) return 1.0;
    vec2 edge = smoothstep(vec2(0.0), vec2(width), coordinates) *
                smoothstep(vec2(1.0), vec2(1.0 - width), coordinates);
    return edge.x * edge.y;
}

float edgeMask(vec3 coordinates) {
    float width = clamp(decal.surfaceParams.w, 0.0, 0.49);
    if (width <= 0.0) return 1.0;
    vec3 edge = smoothstep(vec3(0.0), vec3(width), coordinates) *
                smoothstep(vec3(1.0), vec3(1.0 - width), coordinates);
    return edge.x * edge.y * edge.z;
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
    int projectionMode = int(vExtra.z + 0.5);
    bool useTriplanar = projectionMode == 1;
    bool useSpherical = projectionMode == 2;
    bool useWorld = projectionMode == 3;

    // Planar mode hides grazing faces (where single-axis UVs stretch). Triplanar
    // keeps side faces and only rejects true backfaces.
    float facing = dot(surfaceN, decalFwd);
    if (useSpherical || useTriplanar || useWorld) {
        if (facing < -0.05) discard;
    } else if (facing < 0.1) {
        discard;
    }

    vec4 alb;
    vec4 nrm;
    vec4 prm;
    float edgeFade;
    vec4 nearWorld = cam.invViewProj * vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vec3 nearPos = nearWorld.xyz / max(nearWorld.w, 1e-6);
    vec3 viewWorld = normalize(nearPos - worldPos);
    vec3 viewLocal = normalize(mat3(invModel) * viewWorld);

    if (!useTriplanar && !useSpherical && !useWorld) {
        vec2 decalUV = parallaxUV(local.xy + 0.5, viewLocal);
        if (any(lessThan(decalUV, vec2(0.0))) || any(greaterThan(decalUV, vec2(1.0)))) discard;
        alb = sampleAtlas(decalAlbedo, decalUV);
        nrm = sampleAtlas(decalNormal, decalUV);
        prm = sampleAtlas(decalParams, decalUV);
        edgeFade = edgeMask(decalUV);
    } else if (useTriplanar) {
        // Weights from surface normal in decal-local space so actor rotation /
        // non-uniform scale still align the blend axes with the volume.
        vec3 nLocal = normalize(mat3(invModel) * surfaceN);
        float sharpness = max(vExtra.w, 1.0);
        vec3 w = pow(abs(nLocal), vec3(sharpness));
        w /= max(w.x + w.y + w.z, 1e-5);

        // Axis projections inside the unit box → [0,1] UVs.
        vec2 uvYZ = parallaxUV(local.yz + 0.5, vec3(viewLocal.yz, viewLocal.x));
        vec2 uvXZ = parallaxUV(local.xz + 0.5, vec3(viewLocal.xz, viewLocal.y));
        vec2 uvXY = parallaxUV(local.xy + 0.5, viewLocal);

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
        edgeFade = edgeMask(t);
    } else if (useWorld) {
        float sharpness = max(vExtra.w, 1.0);
        vec3 w = pow(abs(normalize(surfaceN)), vec3(sharpness));
        w /= max(w.x + w.y + w.z, 1e-5);
        const float worldScale = 1.0;
        vec2 uvYZ = fract(parallaxUV(fract(worldPos.yz * worldScale), vec3(viewWorld.yz, viewWorld.x)));
        vec2 uvXZ = fract(parallaxUV(fract(worldPos.xz * worldScale), vec3(viewWorld.xz, viewWorld.y)));
        vec2 uvXY = fract(parallaxUV(fract(worldPos.xy * worldScale), viewWorld));
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
        edgeFade = edgeMask(t);
    } else {
        vec3 direction = normalize(local + vec3(1e-7));
        const float invPi = 0.31830988618;
        vec2 decalUV = vec2(atan(direction.x, direction.z) * (0.5 * invPi) + 0.5,
                            asin(clamp(direction.y, -1.0, 1.0)) * invPi + 0.5);
        vec3 tangent = normalize(vec3(direction.z, 0.0, -direction.x) + vec3(1e-7));
        vec3 bitangent = normalize(cross(direction, tangent));
        vec3 sphericalView = vec3(dot(viewLocal, tangent), dot(viewLocal, bitangent),
                                  dot(viewLocal, direction));
        decalUV = parallaxUV(decalUV, sphericalView);
        decalUV.x = fract(decalUV.x);
        if (decalUV.y < 0.0 || decalUV.y > 1.0) discard;
        alb = sampleAtlas(decalAlbedo, decalUV);
        nrm = sampleAtlas(decalNormal, decalUV);
        prm = sampleAtlas(decalParams, decalUV);
        vec3 t = local + 0.5;
        edgeFade = edgeMask(t);
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
