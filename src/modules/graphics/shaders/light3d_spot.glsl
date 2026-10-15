// Spot attenuation for Light3DGpu packing:
//   spot.xyz = beam direction (world); length may encode local shadow slot
//              |beam| = 1 + (slot+1)/100 when a perspective slot is allocated
//   spot.w   = scale = 1/(cosInner - cosOuter); w <= 0 => not a spot
//   color.a  = bias  = -cosOuter * scale
#ifndef LIGHT3D_SPOT_GLSL
#define LIGHT3D_SPOT_GLSL

float spotAttenuation3D(vec3 lightToSurface, vec4 spot, float spotBias) {
    if (spot.w <= 0.0) return 1.0;
    float beamLen = length(spot.xyz);
    if (beamLen < 1e-6) return 0.0;
    vec3 beam = spot.xyz / beamLen;
    float cosTheta = dot(normalize(lightToSurface), beam);
    return clamp(cosTheta * spot.w + spotBias, 0.0, 1.0);
}

/** @brief Decode perspective local-shadow slot from encoded beam length, or -1. */
int spotLocalShadowSlot(vec4 spot) {
    if (spot.w <= 0.0) return -1;
    float beamLen = length(spot.xyz);
    if (beamLen < 1.005) return -1;
    int slot = int(round((beamLen - 1.0) * 100.0)) - 1;
    if (slot < 0 || slot > 3) return -1;
    return slot;
}

#endif
