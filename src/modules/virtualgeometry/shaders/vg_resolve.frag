// Resolve: sample the visibility buffer (packed depth<<16 | clusterId) and emit
// a shaded color. Cluster id -> stable color via a small hash; depth used for a
// subtle gradient. Optionally visualize LOD level via the misc uniform (x).
#version 450

#include "vg_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    ivec2 px = ivec2(uv * vgU.params.xy);
    uint packed = vgPix[uint(px.y) * uint(vgU.params.x) + uint(px.x)];
    uint cid = vg_clusterOf(packed);
    uint depth = vg_depthOf(packed);
    if (cid == 0u && depth == 0u) {
        color = vec4(0.02, 0.02, 0.04, 1.0);  // empty
        return;
    }
    // Stable per-cluster color hash.
    uint h = cid * 2654435761u;
    vec3 tint = vec3(float((h >> 16) & 0xFFu), float((h >> 8) & 0xFFu), float(h & 0xFFu)) / 255.0;
    tint = normalize(max(tint, vec3(0.05)));
    float depthF = float(depth) / 65535.0;
    color = vec4(tint * (0.35 + 0.65 * (1.0 - depthF)), 1.0);
}
