#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 baryCoord;

void main() {
    // Simple albedo-less hit marker: gray based on barycentrics so the path is observable.
    vec3 color = vec3(baryCoord.x, baryCoord.y, 1.0 - baryCoord.x - baryCoord.y);
    payload = vec4(color, 1.0);
}
