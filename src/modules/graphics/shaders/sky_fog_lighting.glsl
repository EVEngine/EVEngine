// Source sky-atmosphere fog branch maps normalized sun height [.5,.6] to [0,.35].
// Configured radiance is the full-height value; native sun-direction Y is [-1,1].
float skyDirectionalFogFade(float sunY, vec2 elevationRange) {
    if (elevationRange.y <= elevationRange.x) return 1.0;
    return clamp((sunY-elevationRange.x)/(elevationRange.y-elevationRange.x),0.0,1.0);
}
