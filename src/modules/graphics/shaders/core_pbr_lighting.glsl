// Shared core-lighting convention for forward and Hybrid opaque surfaces.
// Keep authored forward appearance when eliminating the opaque forward pass.
const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-4);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 eveCoreShadeLight(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 L, vec3 radiance, float specularFactor) {
    float NdotL = max(dot(N, L), 0.0);
    // Lambert wrap (legacy mesh3d) for dielectrics; metals rely on specular.
    float diffuse = mix(NdotL, NdotL * 0.5 + 0.5, 0.25);

    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04 * specularFactor), albedo, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * max(NdotL, 0.001), 1e-3);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo * diffuse + specular * NdotL) * radiance;
}


vec3 eveHemisphereIrradiance(vec3 N, vec3 ambient, vec3 primaryColor) {
    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyIrr = ambient * 1.1 + primaryColor * 0.12;
    vec3 gndIrr = ambient * vec3(0.72, 0.62, 0.52);
    return mix(gndIrr, skyIrr, hemi);
}

vec3 eveCoreIndirect(vec3 N, vec3 primaryL, vec3 primaryColor, vec3 albedo,
                     float metallic, float occlusion, vec3 irradiance) {
    vec3 gi = albedo * irradiance * (1.0 - metallic) * occlusion;
    float wrap = max(dot(N, primaryL) * 0.5 + 0.5, 0.0);
    gi += albedo * primaryColor * (wrap * wrap) * 0.06 * (1.0 - metallic) * occlusion;
    return gi;
}
