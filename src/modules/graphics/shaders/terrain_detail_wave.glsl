// Unity TerrainEngine.cginc-compatible waving grass core. The caller supplies
// the already transformed world position and a bottom-to-top vertex mask.
void terrainDetailFastSinCos(vec4 value, out vec4 sineValue, out vec4 cosineValue) {
    value = value * 6.408849 - 3.1415927;
    vec4 r5 = value * value;
    vec4 r6 = r5 * r5;
    vec4 r7 = r6 * r5;
    vec4 r8 = r6 * r6;
    vec4 r1 = r5 * value;
    vec4 r2 = r1 * r5;
    vec4 r3 = r2 * r5;
    sineValue = value + r1 * -0.16161616 + r2 * 0.0083333 + r3 * -0.00019841;
    cosineValue = 1.0 + r5 * -0.5 + r6 * 0.041666666 + r7 * -0.0013888889 + r8 * 0.000024801587;
}

vec3 terrainDetailWave(inout vec3 worldPosition, float heightMask, GpuInstance instance) {
    if (instance.terrainWave.w < 0.5) return vec3(1.0);
    vec4 waveXSize = vec4(0.012, 0.02, 0.06, 0.024) * instance.terrainWave.y;
    vec4 waveZSize = vec4(0.006, 0.02, 0.02, 0.05) * instance.terrainWave.y;
    vec4 waves = worldPosition.x * waveXSize + worldPosition.z * waveZSize;
    waves += instance.terrainWave.x * vec4(0.3, 0.5, 0.4, 1.2) * 4.0;
    vec4 sineValue, cosineValue;
    terrainDetailFastSinCos(fract(waves), sineValue, cosineValue);
    sineValue *= sineValue;
    sineValue *= sineValue;
    float lighting = dot(sineValue, normalize(vec4(1.0, 1.0, 0.4, 0.2))) * 0.7;
    sineValue *= max(heightMask, 0.0);
    vec2 move;
    move.x = dot(sineValue, vec4(0.012, 0.02, -0.06, 0.048) * 2.0);
    move.y = dot(sineValue, vec4(0.006, 0.02, -0.02, 0.1));
    worldPosition.xz -= move * instance.terrainWave.z;
    vec3 waveColor = mix(vec3(0.5), instance.terrainWaveTint.rgb, vec3(lighting));
    return 2.0 * waveColor;
}
