#ifndef EVE_PT_SCENE
#define EVE_PT_SCENE

// Private std430 ABI. All offsets index elements, never byte offsets.
struct PtVertex {
    vec4 position;
    vec4 normal;
    vec4 texcoord;
    vec4 tangent;
    vec4 bitangent;
};
struct PtTriangle {
    uvec4 indicesMaterial;
    uvec4 emitter; // x: instance-local area-light index, or PT_INVALID
};
struct PtInstance {
    mat4 objectToWorld;
    mat4 normalToWorld;
    uvec4 offsets; // first triangle, first vertex, first area light, reserved
};
struct PtMaterial {
    vec4 baseColor;
    vec4 emissionRoughness;
    vec4 parameters; // metallic, alpha cutoff, alpha mode (0 opaque / 1 mask / 2 blend), normal scale
    uvec4 textures; // base color, metallic/roughness, emission, normal (PT_INVALID if absent)
};
struct PtTexture {
    uvec4 image; // word offset, width, height, format/sampler flags
    vec4 scaleOffset;
    vec4 parameters; // cos/sin of UV rotation; remaining fields reserved
};
struct PtLight {
    vec4 p0Area; // triangle vertex / point position / direction toward light; area
    vec4 p1Kind; // triangle vertex; kind: 0 area, 1 point, 2 directional, 3 spot
    vec4 p2; // area: w holds primitive index bits
    vec4 emission; // radiance / intensity; area w holds instance index bits
};
struct PtPayload {
    vec2 barycentric;
    float distance;
    uint instance;
    uint primitive;
    uint opacitySeed; // immutable during traversal: duplicate any-hit calls make the same decision
};

#endif
