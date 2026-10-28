#ifndef EVE_VIRTUAL_TEXTURE_GLSL
#define EVE_VIRTUAL_TEXTURE_GLSL

// UE-style packed feedback id: pageX (12) | pageY (12) | mip (8).
uint packVirtualTextureFeedback(vec2 sourceUv, vec4 virtualInfo, float mipLevel) {
    vec2 pageCounts = max(virtualInfo.yz, vec2(1.0));
    vec2 virtualCoord = fract(sourceUv) * pageCounts;
    ivec2 page = clamp(ivec2(floor(virtualCoord)), ivec2(0),
                       ivec2(pageCounts) - ivec2(1));
    uint mip = uint(clamp(mipLevel, 0.0, 255.0));
    return uint(page.x) | (uint(page.y) << 12u) | (mip << 24u);
}

vec4 sampleVirtualTexture(sampler2D atlas, sampler2D pageTable, vec2 sourceUv,
                          vec4 virtualInfo, vec4 atlasInfo) {
    vec2 uv = fract(sourceUv);
    vec2 pageCounts = max(virtualInfo.yz, vec2(1.0));
    vec2 slots = max(atlasInfo.xy, vec2(1.0));
    vec2 virtualCoord = uv * pageCounts;
    ivec2 page = clamp(ivec2(floor(virtualCoord)), ivec2(0),
                       textureSize(pageTable, 0) - ivec2(1));
    vec4 entry = texelFetch(pageTable, page, 0);
    bool resident = entry.b > 0.5;
    vec2 payloadUv = resident ? fract(virtualCoord) : uv;
    vec2 slot = floor(entry.rg * slots);
    float gutter = clamp(virtualInfo.w, 0.0, 0.499);
    vec2 physicalUv = (slot + mix(vec2(gutter), vec2(1.0 - gutter), payloadUv)) / slots;
    vec2 derivativeScale = (resident ? pageCounts : vec2(1.0)) *
                           (1.0 - 2.0 * gutter) / slots;
    return textureGrad(atlas, physicalUv, dFdx(sourceUv) * derivativeScale,
                       dFdy(sourceUv) * derivativeScale);
}

#endif
