#version 450
// Interior Mapping (pre-projected 2D atlas) — based on
// 「Interior Mapping室内映射技术拆解」: tangent-space ray–AABB,
// perspective correction, atlas room selection, optional foreground
// ray-march against procedural furniture bounds.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;
layout(location = 0) out vec4 outColor;

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo;
    vec4 cloud;
    vec4 cloudWind;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(push_constant) uniform Externals {
    float data[32];
} pc;

// pc layout (declareFloat order):
// 0 atlasColumns  1 atlasRows  2 perspective  3 roomDepth
// 4 randomRoom    5 randomSeed 6 fgSteps      7 time
// 8 showFg        9 frameWidth

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 hash22(vec2 p) {
    float n = hash21(p);
    return vec2(n, hash21(p + n + 19.19));
}

// Cotangent frame (Mesh3D has no mesh tangents).
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invMax, B * invMax, N);
}

// Far-hit only ray vs unit box [-1,1]^3 (Joost / article form).
vec3 rayBoxFarHit(vec3 rayPos, vec3 rayDir) {
    vec3 safeDir = vec3(
        abs(rayDir.x) < 1e-5 ? (rayDir.x < 0.0 ? -1e-5 : 1e-5) : rayDir.x,
        abs(rayDir.y) < 1e-5 ? (rayDir.y < 0.0 ? -1e-5 : 1e-5) : rayDir.y,
        abs(rayDir.z) < 1e-5 ? (rayDir.z < 0.0 ? -1e-5 : 1e-5) : rayDir.z);
    vec3 invDir = 1.0 / safeDir;
    vec3 tFar = abs(invDir) - rayPos * invDir;
    float t = min(min(tFar.x, tFar.y), tFar.z);
    return rayPos + t * rayDir;
}

// Per-room furniture AABB in room space [-1,1], z from -1 (window) to +1 (back).
// Returns bounds as (minXYZ, maxXYZ) packed; empty if no furniture.
bool furnitureBounds(vec2 roomIndex, out vec3 bmin, out vec3 bmax) {
    float style = hash21(roomIndex + 3.17);
    if (style < 0.18) {
        // empty room
        return false;
    }
    if (style < 0.45) {
        // desk against back wall
        bmin = vec3(-0.55, -1.0, 0.35);
        bmax = vec3(0.55, -0.35, 0.95);
        return true;
    }
    if (style < 0.70) {
        // sofa / cabinet left
        bmin = vec3(-0.95, -1.0, -0.15);
        bmax = vec3(-0.35, -0.15, 0.55);
        return true;
    }
    // tall plant / column right-center
    bmin = vec3(0.35, -1.0, -0.25);
    bmax = vec3(0.75, 0.35, 0.35);
    return true;
}

bool insideAABB(vec3 p, vec3 bmin, vec3 bmax) {
    return all(greaterThanEqual(p, bmin)) && all(lessThanEqual(p, bmax));
}

bool rayMarchFurniture(inout vec3 pos, vec3 rayDir, int steps, vec3 bmin, vec3 bmax) {
    // March from window (z~-1) toward back; pos is room-space.
    float maxZ = bmax.z + 0.02;
    float stepDepth = max(maxZ - pos.z, 0.05) / float(max(steps, 1));
    vec3 stepSize = rayDir * (stepDepth / max(abs(rayDir.z), 1e-5));
    vec3 prev = pos;
    for (int i = 0; i < 32; ++i) {
        if (i >= steps) break;
        if (pos.z > maxZ || abs(pos.x) > 1.05 || abs(pos.y) > 1.05) break;
        if (insideAABB(pos, bmin, bmax)) {
            vec3 lo = prev;
            vec3 hi = pos;
            for (int j = 0; j < 5; ++j) {
                vec3 mid = 0.5 * (lo + hi);
                if (insideAABB(mid, bmin, bmax)) hi = mid;
                else lo = mid;
            }
            pos = hi;
            return true;
        }
        prev = pos;
        pos += stepSize;
    }
    return false;
}

vec3 furnitureColor(vec2 roomIndex, vec3 hit) {
    float h = hash21(roomIndex + 9.1);
    vec3 wood = vec3(0.45, 0.28, 0.14);
    vec3 cloth = vec3(0.25 + 0.35 * h, 0.22, 0.35 + 0.2 * (1.0 - h));
    vec3 metal = vec3(0.55, 0.58, 0.62);
    float band = smoothstep(-0.4, -0.2, hit.y);
    return mix(wood, mix(cloth, metal, step(0.7, h)), band);
}

void main() {
    float atlasColumns = max(pc.data[0], 1.0);
    float atlasRows = max(pc.data[1], 1.0);
    float perspective = max(pc.data[2], 0.05);
    float roomDepth = clamp(pc.data[3], 0.05, 0.95);
    float randomRoom = pc.data[4];
    float randomSeed = pc.data[5];
    int fgSteps = int(clamp(pc.data[6], 1.0, 32.0));
    float showFg = pc.data[8];
    float frameWidth = clamp(pc.data[9], 0.0, 0.25);

    vec2 uv = vUV;
    vec2 tileIndex = floor(uv);
    vec2 localUV = fract(uv);
    vec2 atlasSize = floor(max(vec2(atlasColumns, atlasRows), vec2(1.0)));

    vec2 roomIndex = tileIndex - floor(tileIndex / atlasSize) * atlasSize;
    if (randomRoom > 0.5) {
        vec2 rnd = hash22(tileIndex + randomSeed * 7.31);
        roomIndex = floor(rnd * atlasSize);
    }

    vec3 N = normalize(vNormal);
    mat3 TBN = cotangentFrame(N, vWorldPos, uv);
    // Article: view from camera to surface, then into tangent space.
    vec3 viewWS = vWorldPos - vCameraPos;
    vec3 tangentViewDir = transpose(TBN) * viewWS; // world→tangent (TBN columns are T,B,N)

    // Depth scale: remap roomDepth like the cubemap path (farFrac).
    float depthScale = 1.0 / max(1.0 - roomDepth, 1e-4) - 1.0;
    tangentViewDir.z *= max(depthScale, 1e-3);

    vec3 rayDir = tangentViewDir;
    // March into the room (+Z of tangent space after abs on Z).
    rayDir.z = max(abs(rayDir.z), 1e-4);
    // Window plane at z = -1, XY in [-1,1].
    vec3 rayPos = vec3(localUV * 2.0 - 1.0, -1.0);

    vec3 hitPos = rayBoxFarHit(rayPos, rayDir);

    // Perspective correction (similar triangles): Scale = B / (B + d).
    float hitDepthNorm = clamp(hitPos.z * 0.5 + 0.5, 0.0, 1.0);
    float perspectiveScale = perspective / (perspective + hitDepthNorm);
    vec2 roomUV = hitPos.xy * perspectiveScale;
    roomUV = roomUV * 0.5 + 0.5;
    roomUV = clamp(roomUV, 0.001, 0.999);

    vec2 atlasUV = (roomIndex + roomUV) / atlasSize;
    vec4 bgColor = texture(albedoSampler, atlasUV);

    vec3 color = bgColor.rgb;
    bool hitFg = false;
    if (showFg > 0.5) {
        vec3 bmin, bmax;
        if (furnitureBounds(roomIndex, bmin, bmax)) {
            vec3 fgPos = rayPos;
            hitFg = rayMarchFurniture(fgPos, rayDir, fgSteps, bmin, bmax);
            if (hitFg) {
                color = furnitureColor(roomIndex, fgPos);
                // Slight lighting from room-space normal of nearest furniture face.
                float shade = 0.75 + 0.25 * max(-fgPos.z, 0.0);
                color *= shade;
            }
        }
    }

    // Window frame (mullions) in local UV.
    float fx = min(localUV.x, 1.0 - localUV.x);
    float fy = min(localUV.y, 1.0 - localUV.y);
    float frame = 1.0 - smoothstep(frameWidth * 0.55, frameWidth, min(fx, fy));
    float mullionX = 1.0 - smoothstep(frameWidth * 0.2, frameWidth * 0.45, abs(localUV.x - 0.5));
    float mullionY = 1.0 - smoothstep(frameWidth * 0.2, frameWidth * 0.45, abs(localUV.y - 0.5));
    float mullion = max(mullionX, mullionY) * 0.85;
    float frameMask = max(frame, mullion);
    vec3 frameCol = vec3(0.08, 0.09, 0.11);
    color = mix(color, frameCol, frameMask);

    // Grazing-angle glass fresnel (FH4 tip from the article lineage).
    vec3 viewDir = normalize(vCameraPos - vWorldPos);
    float ndotv = clamp(dot(N, viewDir), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 3.0);
    vec3 sky = vec3(0.55, 0.68, 0.85);
    color = mix(color, sky, fresnel * 0.35 * (1.0 - frameMask));

    // Mild exterior lighting on the facade plane.
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    float diff = 0.55 + 0.45 * max(dot(N, L), 0.0);
    color *= mix(1.0, diff, 0.25);
    color += ubo.ambient.rgb * 0.08;

    outColor = vec4(color * vTint.rgb, vTint.a);
}
