#version 450
// Lookdev optical probe for a uniformly scaled solid sphere. Not a general mesh-volume solver.
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor;
    vec4 tint; vec4 cameraPos; vec4 ambient;
} u;
layout(set=0,binding=1) uniform sampler2D opaqueScene;
layout(set=0,binding=2) uniform sampler2D sceneDepth;
layout(set=0,binding=3) uniform samplerCube environmentMap;
layout(push_constant) uniform Controls { float values[32]; } control;
layout(location=0) out vec4 outColor;
float fresnel(float cosine, float ior) {
    float r=(1.0-ior)/(1.0+ior);float f0=r*r;
    return f0+(1.0-f0)*pow(1.0-clamp(cosine,0.0,1.0),5.0);
}
vec3 roomReflection(vec3 origin,vec3 direction){
 vec3 safeDirection=mix(vec3(1e-5),direction,greaterThan(abs(direction),vec3(1e-5)));
 vec3 boundary=mix(vec3(control.values[3],control.values[4],control.values[5]),
                   vec3(control.values[6],control.values[7],control.values[8]),greaterThan(direction,vec3(0)));
 vec3 distances=(boundary-origin)/safeDirection;
 distances=mix(vec3(1e20),distances,greaterThan(abs(direction),vec3(1e-5)));
 float distance=max(0.0,min(distances.x,min(distances.y,distances.z)));
 vec3 hit=origin+direction*distance;
 return textureLod(environmentMap,normalize(hit),0).rgb*u.lightColor.w;
}
void main() {
    float ior=clamp(control.values[0],1.001,2.5);
    vec3 n=normalize(vNormal),incident=normalize(vWorldPos-vCameraPos);
    vec3 inside=refract(incident,n,1.0/ior);
    vec3 center=u.model[3].xyz;
    float distanceInGlass=max(0.0,-2.0*dot(vWorldPos-center,inside));
    vec3 exitPosition=vWorldPos+inside*distanceInGlass;
    vec3 exitNormal=normalize(exitPosition-center);
    vec3 outgoing=refract(inside,-exitNormal,ior);
    float reflection=fresnel(dot(-incident,n),ior);
    float exitReflection=fresnel(dot(inside,exitNormal),ior);
    reflection=reflection+(1.0-reflection)*exitReflection;
    vec3 reflected=roomReflection(vWorldPos,reflect(incident,n));
    if(dot(outgoing,outgoing)<0.001){outColor=vec4(reflected,1.0);return;}
    // Trace the exiting ray against the depth of the actual opaque scene.
    // Uncovered or off-screen rays use the captured scene backdrop, not another environment.
    mat4 viewProjection=u.mvp*inverse(u.model);
    vec4 entryClip=viewProjection*vec4(vWorldPos,1.0);
    vec2 entryUV=entryClip.xy/entryClip.w*.5+.5;
    vec3 transmitted=texture(opaqueScene,clamp(entryUV,vec2(.001),vec2(.999))).rgb;
    vec3 origin=exitPosition+outgoing*.025;
    float previousGap=-1.0;
    float previousT=0.0;
    float maxDistance=max(control.values[1],.1);
    for(int i=1;i<=128;++i) {
        float t=maxDistance*float(i)/128.0;
        vec3 point=origin+outgoing*t;
        vec4 clip=viewProjection*vec4(point,1.0);
        vec2 uv=clip.xy/max(clip.w,.001)*.5+.5;
        if(clip.w<=0.0 || any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))break;
        vec3 encoded=texture(sceneDepth,uv).rgb;
        float depth=dot(encoded,vec3(1.0,1.0/255.0,1.0/65025.0))*64.0;
        // Perspective clip.w is positive linear eye depth; no version-dependent view UBO offset.
        float rayDepth=clip.w;
        float gap=rayDepth-depth;
        if(depth>.01 && gap>=0.0 && previousGap<0.0 && gap<.25) {
            // Refine the first crossing to keep pedestal edges stable.
            float lo=previousT,hi=t;
            for(int j=0;j<6;++j){
                float mid=(lo+hi)*.5;
                vec3 p=origin+outgoing*mid;
                vec4 c=viewProjection*vec4(p,1);
                vec2 q=c.xy/c.w*.5+.5;
                float z=dot(texture(sceneDepth,q).rgb,vec3(1,1.0/255.0,1.0/65025.0))*64.0;
                if(c.w>z)hi=mid;else lo=mid;
            }
            vec4 c=viewProjection*vec4(origin+outgoing*(lo+hi)*.5,1);
            transmitted=texture(opaqueScene,clamp(c.xy/c.w*.5+.5,vec2(.001),vec2(.999))).rgb;
            break;
        }
        previousGap=depth>.01?gap:-1.0;
        previousT=t;
    }
    // Beer-Lambert attenuation follows actual distance through the solid sphere.
    vec3 absorption=vec3(0.075,0.014,0.040)*max(control.values[2],0.0);
    transmitted*=exp(-absorption*distanceInGlass);
    outColor=vec4(mix(transmitted,reflected,reflection),1.0);
}
