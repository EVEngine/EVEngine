// Shared physical sky radiance, before authored material controls and height fog.
vec3 skyIntegrateRadiance(vec3 direction, bool variableSteps) {
    vec3 origin = vec3(frame.data[16],frame.data[17],frame.data[18]) * .001 +
                  vec3(0,atmosphere.geometry.x,0);
    // The reference projects sub-surface sky cameras five metres above the
    // virtual ground. Geometry/fog still use the original world camera.
    float cameraRadius = length(origin);
    origin = (cameraRadius > 0.0 ? origin / cameraRadius : vec3(0,1,0)) *
             max(cameraRadius, atmosphere.geometry.x + .005);
    vec2 boundary = sphere(origin,direction,atmosphere.geometry.y);
    float start = max(0,boundary.x);
    float end = boundary.y;
    vec2 ground = sphere(origin,direction,atmosphere.geometry.x);
    if (ground.x > 0) end = min(end,ground.x);
    if (end <= start || length(origin) < atmosphere.geometry.x) {
        return vec3(0);
    }
    vec3 lightDir = vec3(frame.data[20],frame.data[21],frame.data[22]);
    vec3 energy = vec3(frame.data[24],frame.data[25],frame.data[26]);
    float cosine = dot(direction,lightDir);
    float rayPhase = 3*(1+cosine*cosine)/(16*PI);
    float g = atmosphere.mieAbsorption.w;
    float miePhase = (1-g*g) /
        (4*PI*pow(max(1e-8,1+g*g-2*g*cosine),1.5));
    vec3 moonDirection=vec3(0,1,0),moonEnergy=vec3(0);
#ifdef SKY_VIEW_PRODUCER
    if(atmosphere.opticalInfo.x>1.0) skyMoonLight(moonDirection,moonEnergy);
#endif
    float moonCosine=dot(direction,moonDirection);
    float moonRayPhase=3.0*(1.0+moonCosine*moonCosine)/(16.0*PI);
    float moonMiePhase=(1.0-g*g)/(4.0*PI*pow(max(1e-8,1.0+g*g-2.0*g*moonCosine),1.5));
    vec3 transmittance=vec3(1), radiance=vec3(0);
    float distance = end-start;
    float count = variableSteps ? mix(4.0,32.0,clamp(distance/150.0,0.0,1.0)) : 64.0;
    float whole = floor(count);
    float wholeDistance = distance * whole / count;
    for (int i=0;i<int(ceil(count));++i) {
        float a = float(i)/whole, b = float(i+1)/whole;
        float t0 = variableSteps ? wholeDistance*a*a : float(i)*distance/count;
        float t1 = variableSteps ? (b>1.0 ? distance : wholeDistance*b*b) : float(i+1)*distance/count;
        float stepLength = t1-t0;
        vec3 point=origin+direction*(start+mix(t0,t1,variableSteps ? .3 : .5));
        vec3 extinction, rayleigh, mie;
        medium(point,extinction,rayleigh,mie);
        vec3 segment=exp(-extinction*stepLength);
        vec3 source=((rayleigh*rayPhase+mie*miePhase)*sunlight(point,lightDir)+
                     multipleScattering(point,lightDir)*(rayleigh+mie))*energy;
        if(any(greaterThan(moonEnergy,vec3(0))))
            source+=((rayleigh*moonRayPhase+mie*moonMiePhase)*sunlight(point,moonDirection)+multipleScattering(point,moonDirection)*(rayleigh+mie))*moonEnergy;
        radiance += transmittance*source*(1-segment)/max(extinction,vec3(1e-8));
        transmittance *= segment;
    }
    return radiance;
}

#include "sky_view_lut.glsl"
