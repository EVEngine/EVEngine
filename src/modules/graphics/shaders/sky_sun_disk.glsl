// UDS Sun_Disk: UE material ArccosineFast uses the degree-one approximation,
// unlike the degree-four acos used by the atmosphere LUT parameterization.
float skySunAcos(float cosine) {
    float x = clamp(abs(cosine),0.0,1.0);
    float angle = (1.5707963267948966-0.156583*x)*sqrt(1.0-x);
    if (cosine<0.0) angle = 3.141592653589793-angle;
    return angle;
}
float skySunDiskMask(float cosine, float radius, float softness) {
    return pow(max(0.0,1.0-clamp(skySunAcos(cosine)/radius,0.0,1.0)),softness);
}
