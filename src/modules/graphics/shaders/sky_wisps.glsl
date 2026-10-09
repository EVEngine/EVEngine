// UDS desktop thin-cloud material arithmetic. Directions here are in UE Z-up
// space, and point along the source light's forward vector (away from light).
// Call the lighting functions per authored dome vertex, then interpolate.
float skySunCenteredGradient(vec3 worldPosition, vec3 sunForward) {
    vec3 inward = -normalize(worldPosition);
    inward.z = (inward.z - sunForward.z) *
        (1.0 + 2.0 * (1.0 - clamp(-sunForward.z, 0.0, 1.0))) + sunForward.z;
    return (dot(normalize(inward), sunForward) + 1.0) * 0.5;
}

float skyMoonCenteredGradient(vec3 worldPosition, vec3 sunForward,
                             vec3 moonForward, float enabled, bool staticClouds) {
    float night = clamp((sunForward.z + (staticClouds ? 0.03 : 0.0)) * 15.0, 0.0, 1.0);
    return (dot(-normalize(worldPosition), moonForward) + 1.0) * 0.5 * night * enabled;
}

vec3 skyWispsVertexColor(vec3 actorRelativePosition, vec3 baseColor, vec4 gradient) {
    float highlight = pow(clamp(dot(-normalize(actorRelativePosition), gradient.xyz), 0.0, 1.0), 12.0);
    return baseColor * (vec3(1.0) + vec3(1.2, 0.92498, 0.648) * highlight * gradient.w);
}

float skyWispsLitScale(vec2 interpolatedCelestialGradients, float litIntensity) {
    return (dot(interpolatedCelestialGradients, vec2(1.8)) + 2.3) * litIntensity;
}

float skyWispsOpacityAtPhase(sampler2D wispsTexture, vec2 authoredUV3, float vertexRed,
                            vec2 movement, float phase, float morphAmount, float opacity) {
    vec2 displacement = pow(max(vertexRed, 0.0), 5.0) * morphAmount * movement;
    vec2 firstUV = authoredUV3 + displacement * (fract(phase + 0.5) - 0.5);
    vec2 secondUV = vec2(1.0) - authoredUV3 - displacement * (fract(phase) - 0.5);
    float blend = (sin((phase + 0.75) * 6.283185307179586) + 1.0) * 0.5;
    return clamp(mix(texture(wispsTexture, firstUV).r, texture(wispsTexture, secondUV).r, blend) * opacity,
                 0.0, 1.0);
}

float skyWispsOpacity(sampler2D wispsTexture, vec2 authoredUV3, float vertexRed,
                     vec2 movement, float cloudTime, float morphRate,
                     float morphPeriod, float morphAmount, float opacity) {
    return skyWispsOpacityAtPhase(wispsTexture, authoredUV3, vertexRed, movement,
        fract(cloudTime*morphRate/morphPeriod), morphAmount, opacity);
}
