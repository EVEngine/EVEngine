// UDS Contrast_Control applies to the composed linear material radiance.
vec3 skyMaterialContrast(vec3 radiance, vec3 controls) {
    return radiance * max(vec3(0), vec3(1) +
        (min(radiance / controls.y, vec3(3)) - vec3(1)) * controls.x) * controls.z;
}
