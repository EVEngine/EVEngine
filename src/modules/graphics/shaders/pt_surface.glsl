#ifndef EVE_PT_SURFACE
#define EVE_PT_SURFACE

layout(binding = 3, std430) readonly buffer Vertices { PtVertex vertices[]; };
layout(binding = 4, std430) readonly buffer Triangles { PtTriangle triangles[]; };
layout(binding = 5, std430) readonly buffer Instances { PtInstance instances[]; };
layout(binding = 6, std430) readonly buffer Materials { PtMaterial materials[]; };
layout(binding = 10, std430) readonly buffer ColorTexels { uint colorTexels[]; };
layout(binding = 12, std430) readonly buffer Textures { PtTexture textures[]; };

// info.w: packed RGBA8=1, repeat U=2/V=4, linear=8, sRGB=16, premultiplied=32.
// mirror U=64/V=128; CPU float spans default to linear/repeat.
int addressTexel(int coordinate, int size, bool repeat, bool mirror) {
    if (repeat) return (coordinate % size + size) % size;
    if (mirror) {
        int p = (coordinate % (2 * size) + 2 * size) % (2 * size);
        return p < size ? p : 2 * size - 1 - p;
    }
    return clamp(coordinate, 0, size - 1);
}
float addressUv(float uv, bool repeat, bool mirror) {
    if (repeat) return fract(uv);
    if (mirror) return 1.0 - abs(2.0 * fract(uv * 0.5) - 1.0);
    return clamp(uv, 0.0, 1.0);
}
vec4 colorTexel(uvec4 info, ivec2 coordinate) {
    ivec2 size = ivec2(info.yz);
    bool packed = (info.w & 1u) != 0u;
    ivec2 wrapped = ivec2(addressTexel(coordinate.x, size.x, (info.w & 2u) != 0u, (info.w & 64u) != 0u),
                          addressTexel(coordinate.y, size.y, (info.w & 4u) != 0u, (info.w & 128u) != 0u));
    uint pixel = uint(wrapped.y) * info.y + uint(wrapped.x);
    if (!packed) {
        uint offset = info.x + pixel * 4u;
        return uintBitsToFloat(uvec4(colorTexels[offset], colorTexels[offset + 1u],
                                    colorTexels[offset + 2u], colorTexels[offset + 3u]));
    }
    vec4 color = unpackUnorm4x8(colorTexels[info.x + pixel]);
    if ((info.w & 32u) != 0u) color.rgb = color.a > 0.0 ? clamp(color.rgb / color.a, 0.0, 1.0) : vec3(0.0);
    if ((info.w & 16u) != 0u)
        color.rgb = mix(pow((color.rgb + 0.055) / 1.055, vec3(2.4)), color.rgb / 12.92,
                        lessThanEqual(color.rgb, vec3(0.04045)));
    return color;
}

vec4 sampleBaseColor(uvec4 info, vec2 uv) {
    uv.x = addressUv(uv.x, (info.w & 2u) != 0u, (info.w & 64u) != 0u);
    uv.y = addressUv(uv.y, (info.w & 4u) != 0u, (info.w & 128u) != 0u);
    if ((info.w & 8u) == 0u) return colorTexel(info, ivec2(floor(uv * vec2(info.yz))));
    vec2 position = uv * vec2(info.yz) - 0.5;
    ivec2 corner = ivec2(floor(position));
    vec2 weight = fract(position);
    return mix(mix(colorTexel(info, corner), colorTexel(info, corner + ivec2(1, 0)), weight.x),
               mix(colorTexel(info, corner + ivec2(0, 1)), colorTexel(info, corner + ivec2(1, 1)), weight.x), weight.y);
}

vec4 samplePathTexture(uint index, vec2 uv) {
    PtTexture texture = textures[index];
    uv *= texture.scaleOffset.xy;
    float c = texture.parameters.x, s = texture.parameters.y;
    uv = mat2(c, s, -s, c) * uv + texture.scaleOffset.zw;
    return sampleBaseColor(texture.image, uv);
}

vec2 surfaceUv(PtInstance instance, PtTriangle triangle, vec2 barycentric) {
    vec2 a = vertices[instance.offsets.y + triangle.indicesMaterial.x].texcoord.xy;
    vec2 b = vertices[instance.offsets.y + triangle.indicesMaterial.y].texcoord.xy;
    vec2 c = vertices[instance.offsets.y + triangle.indicesMaterial.z].texcoord.xy;
    return (1.0 - barycentric.x - barycentric.y) * a + barycentric.x * b + barycentric.y * c;
}

// Identical coverage for traversal and explicit emitter sampling.
float surfaceCoverage(uint instanceIndex, uint primitiveIndex, vec2 barycentric) {
    PtInstance instance = instances[instanceIndex];
    PtTriangle triangle = triangles[instance.offsets.x + primitiveIndex];
    PtMaterial material = materials[triangle.indicesMaterial.w];
    if (material.parameters.z == 0.0) return 1.0;
    float alpha = material.baseColor.a;
    if (material.textures.x != PT_INVALID)
        alpha *= samplePathTexture(material.textures.x, surfaceUv(instance, triangle, barycentric)).a;
    return material.parameters.z == 1.0 ? (alpha >= material.parameters.y ? 1.0 : 0.0) : alpha;
}

vec3 surfaceEmission(uint instanceIndex, uint primitiveIndex, vec2 barycentric) {
    PtInstance instance = instances[instanceIndex];
    PtTriangle triangle = triangles[instance.offsets.x + primitiveIndex];
    PtMaterial material = materials[triangle.indicesMaterial.w];
    vec3 emission = material.emissionRoughness.rgb;
    if (material.textures.z != PT_INVALID)
        emission *= samplePathTexture(material.textures.z, surfaceUv(instance, triangle, barycentric)).rgb;
    return emission;
}

#endif
