// Shared background composition for atmosphere and sky material layers.
vec3 applyHeightFogColor(vec3 radiance, vec3 direction, vec3 baseColor) {
    if (atmosphere.fogDensity.x <= 0) return radiance;
    float height = min(frame.data[17],atmosphere.fogDensity.z+655.36);
    vec3 receiver = direction*atmosphere.fogGeometry.z;
    receiver.y += frame.data[17]-height;
    float distance = length(receiver);
    vec3 ray = receiver/distance;
    float start = atmosphere.fogDensity.w;
    float rayLength = max(0,distance-start);
    float startHeight = height + ray.y*start;
    float originDensity = atmosphere.fogDensity.x *
        exp2(clamp(-atmosphere.fogDensity.y*(startHeight-atmosphere.fogDensity.z),-125,126));
    float falloff = max(-127,atmosphere.fogDensity.y*ray.y*rayLength);
    float integral = abs(falloff)>1e-4 ? (1-exp2(-falloff))/falloff :
        log(2.0)-.5*log(2.0)*log(2.0)*falloff;
    float integralDensity = originDensity*integral;
    float transmission = max(exp2(-integralDensity*rayLength),1-atmosphere.fogColor.w);
    float directionalTransmission = exp2(-integralDensity*max(0,rayLength-atmosphere.fogGeometry.x));
    vec3 light = vec3(frame.data[20],frame.data[21],frame.data[22]);
    float lobe = pow(max(0,dot(ray,light)),atmosphere.fogDirectional.w)/(4*PI);
    return radiance*transmission + (baseColor+fogAmbient)*(1-transmission) +
        fogSun*lobe*(1-directionalTransmission);
}

vec3 applyHeightFog(vec3 radiance, vec3 direction) {
    return applyHeightFogColor(radiance,direction,atmosphere.fogColor.rgb);
}
