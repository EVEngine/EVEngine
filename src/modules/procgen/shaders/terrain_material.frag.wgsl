struct Light3D { posRadius: vec4f, color: vec4f, };
struct Frame {
    mvp: mat4x4f,
    model: mat4x4f,
    lightDir: vec4f,
    lightColor: vec4f,
    tint: vec4f,
    cameraPos: vec4f,
    ambient: vec4f,
    lights: array<Light3D, 8>,
    texBomb: vec4f,
    parallax: vec4f,
    surface: vec4f,
    view: mat4x4f,
    clipInfo: vec4f,
};
struct Externals { data: array<f32, 32>, };
struct FSIn {
    @location(0) normal: vec3f,
    @location(1) uv: vec2f,
    @location(2) tint: vec4f,
    @location(3) worldPos: vec3f,
    @location(4) cameraPos: vec3f,
    @location(5) viewPos: vec3f,
};

@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var controlMap: texture_2d<f32>;
@group(0) @binding(7) var controlSampler: sampler;
@group(0) @binding(15) var<uniform> terrain: Externals;
@group(0) @binding(22) var albedoAtlas: texture_2d<f32>;
@group(0) @binding(23) var albedoSampler: sampler;
@group(0) @binding(24) var normalAtlas: texture_2d<f32>;
@group(0) @binding(25) var normalSampler: sampler;
@group(0) @binding(26) var maskAtlas: texture_2d<f32>;
@group(0) @binding(27) var maskSampler: sampler;
@group(0) @binding(28) var holesMap: texture_2d<f32>;
@group(0) @binding(29) var holesSampler: sampler;

fn packedParameter(parameter: i32, layer: i32) -> f32 {
    let sample = textureLoad(holesMap, vec2i(parameter, layer), 0);
    let bytes = vec4u(sample * 255.0 + 0.5);
    let bits = bytes.r | (bytes.g << 8u) | (bytes.b << 16u) | (bytes.a << 24u);
    return bitcast<f32>(bits);
}

fn packedWeight(uv: vec2f, layer: i32) -> f32 {
    let group = layer >> 2;
    let cell = vec2f(f32(group % 3), f32(group / 3));
    let control = max(textureSample(controlMap, controlSampler, (uv + cell) / vec2f(3.0, 2.0)),
                      vec4f(0.0));
    return control[layer & 3];
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4f {
    let packed = terrain.data[28] > 1.5;
    let controlUv = select(input.uv, input.uv / vec2f(3.0, 2.0), packed);
    var weights = max(textureSample(controlMap, controlSampler, controlUv), vec4f(0.0));
    if (!packed) { weights /= max(dot(weights, vec4f(1.0)), 1e-5); }
    let legacy = mat4x4f(vec4f(0.34, 0.235, 0.115, 0.0),
                         vec4f(0.075, 0.255, 0.055, 0.0),
                         vec4f(0.285, 0.275, 0.255, 0.0),
                         vec4f(0.78, 0.82, 0.86, 0.0));
    var albedo = (legacy * weights).rgb;
    var tangentNormal = vec3f(0.0, 0.0, 1.0);
    var metallic = 0.0;
    var smoothness = dot(weights, vec4f(0.18, 0.06, 0.32, 0.48));
    var snowWeight = weights.a;
    if (packed) {
        var albedoSum = vec3f(0.0);
        var normalSum = vec3f(0.0);
        var metallicSum = 0.0;
        var smoothnessSum = 0.0;
        var totalWeight = 0.0;
        var layerThreeWeight = 0.0;
        let layerCount = clamp(i32(terrain.data[30] + 0.5), 1, 16);
        for (var layer = 0; layer < layerCount; layer += 1) {
            totalWeight += packedWeight(input.uv, layer);
        }
        for (var layer = 0; layer < layerCount; layer += 1) {
            let weight = select(select(0.0, 1.0, layer == 0),
                                packedWeight(input.uv, layer) / max(totalWeight, 1e-5),
                                totalWeight > 1e-5);
            if (layer == 3) { layerThreeWeight = weight; }
            let layerUv = input.worldPos.xz * vec2f(packedParameter(0, layer), packedParameter(1, layer)) +
                          vec2f(packedParameter(2, layer), packedParameter(3, layer));
            let cell = vec2f(f32(layer & 3), f32(layer >> 2));
            let atlasUv = (fract(layerUv) + cell) * 0.25;
            let layerAlbedo = textureSample(albedoAtlas, albedoSampler, atlasUv);
            var layerNormal = textureSample(normalAtlas, normalSampler, atlasUv).xyz * 2.0 - 1.0;
            layerNormal.x *= packedParameter(5, layer);
            layerNormal.y *= packedParameter(5, layer);
            let layerMask = textureSample(maskAtlas, maskSampler, atlasUv);
            albedoSum += layerAlbedo.rgb * weight;
            normalSum += normalize(layerNormal) * weight;
            metallicSum += layerMask.r * packedParameter(4, layer) * weight;
            smoothnessSum += layerMask.a * packedParameter(6, layer) * weight;
        }
        albedo = albedoSum;
        tangentNormal = normalize(normalSum);
        metallic = clamp(metallicSum, 0.0, 1.0);
        smoothness = clamp(smoothnessSum, 0.0, 1.0);
        snowWeight = layerThreeWeight;
        if (terrain.data[29] > 0.5 &&
            textureSample(controlMap, controlSampler,
                          (input.uv + vec2f(1.0, 1.0)) / vec2f(3.0, 2.0)).r < 0.5) {
            discard;
        }
    } else if (terrain.data[28] > 0.5) {
        var albedoSum = vec3f(0.0);
        var normalSum = vec3f(0.0);
        var metallicSum = 0.0;
        var smoothnessSum = 0.0;
        for (var layer = 0; layer < 4; layer += 1) {
            let st = layer * 4;
            let layerUv = input.worldPos.xz * vec2f(terrain.data[st], terrain.data[st + 1]) +
                          vec2f(terrain.data[st + 2], terrain.data[st + 3]);
            let quadrant = vec2f(f32(layer & 1), f32(layer >> 1));
            let atlasUv = (fract(layerUv) + quadrant) * 0.5;
            let layerAlbedo = textureSample(albedoAtlas, albedoSampler, atlasUv);
            var layerNormal = textureSample(normalAtlas, normalSampler, atlasUv).xyz * 2.0 - 1.0;
            layerNormal.x *= terrain.data[20 + layer];
            layerNormal.y *= terrain.data[20 + layer];
            let layerMask = textureSample(maskAtlas, maskSampler, atlasUv);
            let weight = weights[layer];
            albedoSum += layerAlbedo.rgb * weight;
            normalSum += normalize(layerNormal) * weight;
            metallicSum += layerMask.r * terrain.data[16 + layer] * weight;
            smoothnessSum += layerMask.a * terrain.data[24 + layer] * weight;
        }
        albedo = albedoSum;
        tangentNormal = normalize(normalSum);
        metallic = clamp(metallicSum, 0.0, 1.0);
        smoothness = clamp(smoothnessSum, 0.0, 1.0);
        if (terrain.data[29] > 0.5 && textureSample(holesMap, holesSampler, input.uv).r < 0.5) {
            discard;
        }
    }
    albedo *= input.tint.rgb;
    var normal = normalize(input.normal);
    let view = normalize(input.cameraPos - input.worldPos);
    if (dot(normal, view) < 0.0) { normal = -normal; }
    if (terrain.data[28] > 0.5) {
        let worldDx = dpdx(input.worldPos);
        let worldDy = dpdy(input.worldPos);
        let uvDx = dpdx(input.uv);
        let uvDy = dpdy(input.uv);
        let determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
        if (abs(determinant) > 1e-7) {
            let tangent = normalize((worldDx * uvDy.y - worldDy * uvDx.y) / determinant);
            let bitangent = normalize(cross(normal, tangent));
            normal = normalize(mat3x3f(tangent, bitangent, normal) * tangentNormal);
        }
    }
    let light = normalize(frame.lightDir.xyz);
    let halfVector = normalize(view + light);
    let ndl = max(dot(normal, light), 0.0);
    let roughness = 1.0 - smoothness;
    let specularPower = mix(96.0, 5.0, roughness);
    let specular = pow(max(dot(normal, halfVector), 0.0), specularPower) *
                   mix(mix(0.22, 0.035, roughness), 1.0, metallic);
    let sky = frame.ambient.rgb * mix(vec3f(0.72, 0.64, 0.55), vec3f(1.05), normal.y * 0.5 + 0.5);
    var color = albedo * (sky + frame.lightColor.rgb * (ndl * 0.88 + 0.12)) +
                frame.lightColor.rgb * specular;
    color /= color + vec3f(0.72);
    let nearZ = max(frame.clipInfo.x, 1e-4);
    let farZ = max(frame.clipInfo.y, nearZ + 1e-3);
    let linearDepth = clamp((max(-input.viewPos.z, 0.0) - nearZ) / (farZ - nearZ), 0.0, 1.0);
    return vec4f(color, linearDepth);
}
