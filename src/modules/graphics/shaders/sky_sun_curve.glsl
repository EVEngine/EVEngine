// Four-key, unweighted cubic Hermite curve. Constant extrapolation matches the source asset.
float skySunCurve(vec4 keys[4], float height) {
    float value = keys[3].y;
    if (height <= keys[0].x) return keys[0].y;
    for (int i=0;i<3;++i) {
        if (height < keys[i+1].x) {
            float span = keys[i+1].x-keys[i].x;
            float t = (height-keys[i].x)/span;
            float t2=t*t, t3=t2*t;
            value=(2*t3-3*t2+1)*keys[i].y+(t3-2*t2+t)*span*keys[i].w
                 +(-2*t3+3*t2)*keys[i+1].y+(t3-t2)*span*keys[i+1].z;
            break;
        }
    }
    return clamp(value,0.0,1.0);
}
