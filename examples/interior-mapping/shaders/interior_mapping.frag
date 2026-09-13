#version 450
// Interior Mapping — per-window object space (XY facade, +Z outward).
// Avoids cotangentFrame handedness bugs by reconstructing room-space
// camera from tiled UVs. Window at z=+1, back at z=-1.
// Atlas: scale = B/(B+d), d = 1 - hit.z.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;
layout(location = 0) out vec4 outColor;

struct Light3D { vec4 posRadius; vec4 color; };

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp; mat4 model;
    vec4 lightDirIntensity; vec4 lightColor; vec4 tint; vec4 cameraPos; vec4 ambient;
    Light3D lights[8];
    vec4 texBomb; vec4 parallax; mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;
layout(push_constant) uniform Externals { float data[32]; } pc;

// Facade layout matches main.nut (WIN_X=6, WIN_Y=4, 1 world unit per window).
const float kWinX = 6.0;
const float kWinY = 4.0;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
vec2 hash22(vec2 p) {
    float n = hash21(p);
    return vec2(n, hash21(p + n + 19.19));
}
vec3 rayBoxFarHit(vec3 rayPos, vec3 rayDir) {
    // Slab method: return the FAR intersection with the unit box [-1,1]^3.
    vec3 d = vec3(
        abs(rayDir.x) < 1e-5 ? (rayDir.x < 0.0 ? -1e-5 : 1e-5) : rayDir.x,
        abs(rayDir.y) < 1e-5 ? (rayDir.y < 0.0 ? -1e-5 : 1e-5) : rayDir.y,
        abs(rayDir.z) < 1e-5 ? (rayDir.z < 0.0 ? -1e-5 : 1e-5) : rayDir.z);
    vec3 inv = 1.0 / d;
    vec3 t0 = (-1.0 - rayPos) * inv;
    vec3 t1 = ( 1.0 - rayPos) * inv;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    float tNear = max(max(tMin.x, tMin.y), tMin.z);
    float tFar  = min(min(tMax.x, tMax.y), tMax.z);
    float t = tFar > tNear ? tFar : max(tFar, 0.0);
    return rayPos + t * d;
}
vec2 projectToAtlas(vec3 hit, float perspective) {
    float d = max(1.0 - hit.z, 1e-3);
    float B = max(perspective, 0.05);
    float scale = B / (B + d);
    vec2 uv = hit.xy * scale * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return clamp(uv, 0.001, 0.999);
}
vec3 faceColor(vec3 hit) {
    vec3 a = abs(hit);
    if (a.z >= a.x && a.z >= a.y)
        return hit.z < 0.0 ? vec3(1.0, 0.85, 0.35) : vec3(1.0, 0.0, 1.0);
    if (a.y >= a.x)
        return hit.y > 0.0 ? vec3(0.2, 0.85, 1.0) : vec3(1.0, 0.25, 0.1);
    return hit.x > 0.0 ? vec3(0.2, 1.0, 0.3) : vec3(0.3, 0.35, 1.0);
}
bool furnitureBounds(vec2 roomIndex, out vec3 bmin, out vec3 bmax) {
    float style = hash21(roomIndex + 3.17);
    if (style < 0.22) return false;
    if (style < 0.50) { bmin=vec3(-0.45,-1.0,-0.92); bmax=vec3(0.45,-0.40,-0.40); return true; }
    if (style < 0.75) { bmin=vec3(-0.92,-1.0,-0.55); bmax=vec3(-0.40,-0.20,-0.05); return true; }
    bmin=vec3(0.40,-1.0,-0.40); bmax=vec3(0.72,0.30,-0.05); return true;
}
bool insideAABB(vec3 p, vec3 bmin, vec3 bmax) {
    return all(greaterThanEqual(p, bmin)) && all(lessThanEqual(p, bmax));
}
bool rayMarchFurniture(inout vec3 pos, vec3 rayDir, int steps, vec3 bmin, vec3 bmax) {
    float minZ = bmin.z - 0.02;
    float stepDepth = max(pos.z - minZ, 0.05) / float(max(steps, 1));
    vec3 stepSize = rayDir * (stepDepth / max(abs(rayDir.z), 1e-5));
    vec3 prev = pos;
    for (int i = 0; i < 32; ++i) {
        if (i >= steps) break;
        if (pos.z < minZ || abs(pos.x) > 1.05 || abs(pos.y) > 1.05) break;
        if (insideAABB(pos, bmin, bmax)) {
            vec3 lo = prev, hi = pos;
            for (int j = 0; j < 5; ++j) {
                vec3 mid = 0.5 * (lo + hi);
                if (insideAABB(mid, bmin, bmax)) hi = mid; else lo = mid;
            }
            pos = hi; return true;
        }
        prev = pos; pos += stepSize;
    }
    return false;
}
vec3 furnitureColor(vec2 roomIndex, vec3 hit) {
    float h = hash21(roomIndex + 9.1);
    return mix(vec3(0.45,0.28,0.14),
               mix(vec3(0.25+0.35*h,0.22,0.35), vec3(0.55,0.58,0.62), step(0.7,h)),
               smoothstep(-0.4,-0.2,hit.y));
}

void main() {
    float atlasColumns = max(pc.data[0], 1.0);
    float atlasRows = max(pc.data[1], 1.0);
    float perspective = max(pc.data[2], 0.05);
    float roomDepth = clamp(pc.data[3], 0.05, 0.95);
    float randomRoom = pc.data[4];
    float randomSeed = pc.data[5];
    float fgStepsF = pc.data[6];
    int fgSteps = int(clamp(fgStepsF, 1.0, 32.0));
    float showFg = pc.data[8];
    float frameWidth = clamp(pc.data[9], 0.0, 0.25);
    bool debugFaces = fgStepsF > 90.0;

    vec2 tileIndex = floor(vUV);
    vec2 localUV = fract(vUV);
    vec2 atlasSize = floor(max(vec2(atlasColumns, atlasRows), vec2(1.0)));
    vec2 roomIndex = tileIndex - floor(tileIndex / atlasSize) * atlasSize;
    if (randomRoom > 0.5)
        roomIndex = floor(hash22(tileIndex + randomSeed * 7.31) * atlasSize);

    // World XY of this window's center (mesh: UV0→(-kWinX/2,-kWinY/2), 1 unit/window).
    vec2 winCenterWS = tileIndex + 0.5 - vec2(kWinX, kWinY) * 0.5;

    // Camera in room space: XY relative to window center (window spans [-1,1]),
    // Z so facade world z=0 maps to room z=+1 (window plane).
    vec3 camRoom;
    // Scale so window half-extent (0.5 world) maps to room 1.0.
    camRoom.xy = (vCameraPos.xy - winCenterWS) * 2.0;
    camRoom.z = vCameraPos.z + 1.0;

    vec3 rayPos = vec3(localUV * 2.0 - 1.0, 0.999);
    vec3 rayDir = rayPos - camRoom; // camera → surface (Joost)

    // Depth control: compress/expand the into-room Z before abs.
    float depthScale = 1.0 / max(1.0 - roomDepth, 1e-4) - 1.0;
    rayDir.z *= max(depthScale, 1e-3);
    rayDir.z = -max(abs(rayDir.z), 1e-4);

    vec3 hitPos = rayBoxFarHit(rayPos, rayDir);

    vec3 color;
    if (debugFaces) {
        color = faceColor(hitPos);
    } else {
        vec2 roomUV = projectToAtlas(hitPos, clamp(perspective, 0.05, 2.0));
        color = texture(albedoSampler, (roomIndex + roomUV) / atlasSize).rgb;
        if (showFg > 0.5) {
            vec3 bmin, bmax;
            if (furnitureBounds(roomIndex, bmin, bmax)) {
                vec3 fgPos = rayPos;
                if (rayMarchFurniture(fgPos, rayDir, fgSteps, bmin, bmax)) {
                    color = furnitureColor(roomIndex, fgPos);
                    color *= 0.75 + 0.25 * max(fgPos.z, 0.0);
                }
            }
        }
    }

    float fx = min(localUV.x, 1.0 - localUV.x);
    float fy = min(localUV.y, 1.0 - localUV.y);
    float frameMask = 1.0 - smoothstep(frameWidth * 0.55, frameWidth, min(fx, fy));
    color = mix(color, vec3(0.08, 0.09, 0.11), frameMask);

    if (!debugFaces) {
        vec3 N = normalize(vNormal);
        float fresnel = pow(1.0 - clamp(dot(N, normalize(vCameraPos - vWorldPos)), 0.0, 1.0), 3.0);
        color = mix(color, vec3(0.55, 0.68, 0.85), fresnel * 0.35 * (1.0 - frameMask));
        float diff = 0.55 + 0.45 * max(dot(N, normalize(ubo.lightDirIntensity.xyz)), 0.0);
        color *= mix(1.0, diff, 0.25);
        color += ubo.ambient.rgb * 0.08;
    }

    outColor = vec4(color * vTint.rgb, vTint.a);
}
