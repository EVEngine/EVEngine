#version 450
// t3ssel8r-style grass billboard vertex.
// inPos    = grass ROOT (object space)
// inNormal.x = animation id, inNormal.y = height scale. z: legacy 0/1 or -width / (1+width) for dense/dark.
// inUV     = quad corner in [0,1]^2; (0.5, 0) is the root (bottom center)

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

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
} ubo;

layout(push_constant) uniform Externals {
    float data[32];
} u;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vViewPos;
layout(location = 3) out vec3 vRootPos;
layout(location = 4) out float vInstanceId;
layout(location = 5) out vec4 vTint;
layout(location = 6) out float vAlwaysDark;
layout(location = 7) out vec3 vInstanceTint;

// PW_GeneralWind deformation in world-relative coordinates; root remains authoritative.
vec3 windOffset(vec3 local, vec3 root, vec3 up, vec2 dimensions) {
    float range = abs(u.data[23]);
    float main = u.data[21];
    if (range == 0.0 || main == 0.0) return local;
    vec3 distance = (root - ubo.cameraPos.xyz) / range;
    float attenuation = 1.0 - clamp(dot(distance, distance), 0.0, 1.0);
    attenuation *= attenuation;
    if (attenuation == 0.0) return local;
    float upLength = length(up);
    float scale = max(upLength, 0.4);
    float upDot = clamp(up.y / upLength, 0.0, 1.0);
    dimensions = mix(vec2(dimensions.y * 2.0), dimensions, upDot * upDot) * scale;
    vec3 flex = vec3(u.data[24], u.data[25], u.data[26]) *
        vec3(clamp(main * 3.0, 0.0, 1.0), clamp(main * 2.0, 0.0, 1.0), 1.0 - main * main * 0.5) * main * attenuation * scale;
    vec3 frequency = vec3(u.data[27], u.data[28], u.data[29]) * scale;
    vec3 direction = vec3(u.data[18], u.data[19], u.data[20]);
    float time = -fract(u.data[22] * 6.0) * 6.283185;
    vec3 norm = local / vec3(dimensions.x, dimensions.y, dimensions.x);
    float branch = dot(norm.xz, norm.xz);
    float stem = norm.y;
    float lengthA = dot(local, local);
    vec3 world = root + local;
    float gust = ((sin(time + frequency.x * (root.x + root.y + root.z)) * 0.3 + main * 0.5) +
        (u.data[30] * 0.4 + main) * main) * (u.data[31] * 0.3 + 0.7);
    vec3 tally = vec3(direction.x, 0.0, direction.z) * stem * stem * gust * flex.x;
    gust = gust * 0.7 + 0.3;
    if (u.data[23] > 0.0) {
        vec3 displaced = world + tally * 0.25;
        tally += direction * stem * stem * (sin(time * 2.0 +
            (displaced.x + displaced.y + displaced.z) * frequency.y) * branch * 0.7 + 0.3) * gust * flex.y;
    }
    vec3 offset = local + tally;
    float denominator = dot(offset, offset);
    float normalization = denominator == 0.0 ? 0.0 : clamp(lengthA / denominator, 0.0, 1.0);
    tally = offset * normalization;
    if (u.data[23] > 0.0 && flex.z != 0.0) {
        vec3 wave = sin(vec3(time * 5.0) + (world + tally) * frequency.z);
        return tally + (wave * direction + direction) * vec3(branch, branch * 0.75, branch) *
            (stem * gust * flex.z) * (normalization + 0.5);
    }
    return tally;
}
void main() {
    float width = u.data[2] > 0.0 ? u.data[2] : 0.01;
    float height = u.data[3] > 0.0 ? u.data[3] : 0.01;
    float scale = inNormal.y > 0.0 ? inNormal.y : 1.0;
    float widthScale = inNormal.z < 0.0 ? -inNormal.z : (inNormal.z > 1.0 ? inNormal.z - 1.0 : scale);

    mat4 invModel = inverse(ubo.model);
    vec3 worldUpObj = inverse(mat3(ubo.model)) * vec3(0.0, 1.0, 0.0);
    float upLen = length(worldUpObj);
    worldUpObj = upLen > 1e-5 ? worldUpObj / upLen : vec3(0.0, 1.0, 0.0);

    vec3 camObj = (invModel * vec4(ubo.cameraPos.xyz, 1.0)).xyz;
    vec3 toCam = camObj - inPos;
    toCam = toCam - worldUpObj * dot(toCam, worldUpObj);
    vec3 right;
    float toCamLen = length(toCam);
    if (toCamLen > 1e-4) {
        right = normalize(cross(worldUpObj, toCam));
    } else {
        vec3 fallback = abs(worldUpObj.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        right = normalize(cross(worldUpObj, fallback));
    }

    vec3 obj = inPos + right * (inUV.x - 0.5) * width * widthScale + worldUpObj * inUV.y * height * scale;
    vec4 rootWorld = ubo.model * vec4(inPos, 1.0);
    if (u.data[23] != 0.0 && u.data[21] != 0.0) {
        mat3 model3 = mat3(ubo.model);
        obj = inPos + inverse(model3) * windOffset(model3 * (obj - inPos), rootWorld.xyz,
            model3 * worldUpObj * scale, vec2(width, height));
    }
    vec4 world = ubo.model * vec4(obj, 1.0);

    gl_Position = ubo.mvp * vec4(obj, 1.0);
    vUV = inUV;
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    vRootPos = rootWorld.xyz;
    if (inNormal.x < 0.0) {
        float packed = -inNormal.x - 1.0;
        vInstanceTint = vec3(floor(packed / 65536.0), floor(mod(packed, 65536.0) / 256.0), mod(packed, 256.0)) / 255.0;
        vInstanceId = fract(sin(dot(inPos, vec3(12.9898, 78.233, 37.719))) * 43758.5453) * 65535.0;
    } else {
        vInstanceTint = vec3(1.0);
        vInstanceId = inNormal.x;
    }
    vTint = ubo.tint;
    vAlwaysDark = inNormal.z > 0.0 ? 1.0 : 0.0;
}
