// Adaptive CSM helpers: each cascade may occupy only a top-left viewport of its
// depth-array layer. `shadow.cascadeUvScale` must be declared on ShadowFrame
// immediately after `cascadeTexel` (see ShadowUBO).

float eveCascadeUvScale(int cascade) {
    float s = cascade == 0 ? shadow.cascadeUvScale.x
                           : (cascade == 1 ? shadow.cascadeUvScale.y : shadow.cascadeUvScale.z);
    return max(s, 1e-4);
}

float eveCascadeAtlasSize() {
    return max(shadow.cascadeUvScale.w, 1.0);
}

float eveCascadeMapSize(int cascade) {
    return eveCascadeAtlasSize() * eveCascadeUvScale(cascade);
}
