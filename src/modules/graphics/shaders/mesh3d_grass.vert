#version 450
// t3ssel8r-style grass billboard vertex.
// inPos    = grass ROOT (object space)
// inNormal.x = instance id, inNormal.y = scale, inNormal.z = alwaysDark (0/1)
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

void main() {
    float width = max(u.data[2], 0.01);
    float height = max(u.data[3], 0.01);
    float scale = max(inNormal.y, 0.05);

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

    vec3 obj = inPos + right * (inUV.x - 0.5) * width * scale + worldUpObj * inUV.y * height * scale;
    vec4 world = ubo.model * vec4(obj, 1.0);
    vec4 rootWorld = ubo.model * vec4(inPos, 1.0);

    gl_Position = ubo.mvp * vec4(obj, 1.0);
    vUV = inUV;
    vWorldPos = world.xyz;
    vViewPos = (ubo.view * world).xyz;
    vRootPos = rootWorld.xyz;
    vInstanceId = inNormal.x;
    vTint = ubo.tint;
    vAlwaysDark = inNormal.z;
}
