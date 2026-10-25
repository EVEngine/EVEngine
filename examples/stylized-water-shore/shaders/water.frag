#version 450
#extension GL_GOOGLE_include_directive : enable
#include "wave.glsl"
layout(location=0) in vec3 normal;
layout(location=1) in vec2 shore;
layout(location=2) in vec4 tint;
layout(location=3) in vec3 world;
layout(location=4) in vec3 camera;
layout(location=5) in vec3 viewPosition;
struct Light3D { vec4 posRadius; vec4 color; vec4 spot; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind; vec4 bindlessEnv;
    vec4 envProbeCenter; vec4 envProbeExtent; vec4 skinInfo;
    vec4 reflectionProbeCenter[2]; vec4 reflectionProbeExtent[2];
} frame;
layout(set=0,binding=1) uniform sampler2D sceneColor;
#ifdef INTERACTION
// Existing Water binding 2 contains the solver depth/velocity field in this variant.
layout(set=0,binding=2) uniform sampler2D physicalField;
#else
layout(set=0,binding=2) uniform sampler2D contactAtlas;
#endif
layout(set=0,binding=3) uniform samplerCube environment;
layout(set=0,binding=6) uniform sampler2D externalReflection;
layout(set=0,binding=7) uniform sampler2D sceneDepth;
layout(push_constant) uniform Params { float data[32]; } p;
layout(location=0) out vec4 outColor;

float hash(vec2 q) { return fract(sin(dot(q,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 q) {
    vec2 i=floor(q),f=fract(q); f=f*f*(3-2*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),
               mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
float square(float x) { return x*x; }
float foamGrain(vec2 q) {
#ifdef INTERACTION
    return noise(q)*.6+noise(q*2.13)*.28+noise(q*4.71)*.12;
#else
    // Warped continuous interference avoids the square cells of thresholded
    // value noise. Suppress unresolved octaves before they shimmer at distance.
    vec2 w=q+vec2(sin(dot(q,vec2(.37,.69))),cos(dot(q,vec2(.61,-.43))))*1.1;
    float footprint=max(length(dFdx(q)),length(dFdy(q)));
    float coarse=sin(dot(w,vec2(1.37,2.11)))*.17+sin(dot(w,vec2(-2.43,1.71)))*.15;
    float fine=sin(dot(w,vec2(4.13,-3.37)))*.10+sin(dot(w,vec2(5.71,6.19)))*.08;
    return .5+coarse*(1.0-smoothstep(.5,2.0,footprint))
        +fine*(1.0-smoothstep(.2,.8,footprint));
#endif
}
#ifndef INTERACTION
vec3 sampleSceneLinear(vec2 uv) {
    ivec2 size=textureSize(sceneColor,0);
    vec2 pixel=clamp(uv*vec2(size)-.5,vec2(0),vec2(size-1));
    ivec2 lo=ivec2(floor(pixel)),hi=min(lo+ivec2(1),size-1);
    vec2 f=fract(pixel);
    return mix(mix(texelFetch(sceneColor,lo,0).rgb,texelFetch(sceneColor,ivec2(hi.x,lo.y),0).rgb,f.x),
        mix(texelFetch(sceneColor,ivec2(lo.x,hi.y),0).rgb,texelFetch(sceneColor,hi,0).rgb,f.x),f.y);
}
// Evaluate continuous scene geometry here: interpolated vertex distances carry
// the triangulation into wave normals, wet edges and foam as the wave advances.
vec3 shoreDistanceGradient(vec2 q) {
    float boundary=-3.8+.7*sin(q.y*.33)+.22*sin(q.y*.91);
    float d=(q.x-boundary)*.96;
    vec2 gradient=vec2(.96,-.96*(.7*.33*cos(q.y*.33)+.22*.91*cos(q.y*.91)));
    for(int i=0;i<16;++i) {
        vec4 shape=texelFetch(contactAtlas,ivec2(i,3),0);
        if(shape.a<.5) break;
        vec4 packed=round(texelFetch(contactAtlas,ivec2(i,2),0)*255.0);
        vec2 center=vec2(packed.r*256.0+packed.g,packed.b*256.0+packed.a)/65535.0*32.0-16.0;
        vec2 radii=shape.rg*2.0,v=(q-center)/radii;
        float len=length(v),scale=min(radii.x,radii.y),rd=(len-1.0)*scale;
        if(rd<d) {d=rd;gradient=v/radii*scale/max(len,.0001);}
    }
    return vec3(d,gradient);
}
float woodContactFoam(vec2 q,float t,float grain,float phase) {
    float foam=0.0;
    for(int i=0;i<16;++i) {
        vec4 shape=texelFetch(contactAtlas,ivec2(i,1),0);
        int kind=int(round(shape.a*3.0));
        if(kind==0) break;
        vec4 packed=round(texelFetch(contactAtlas,ivec2(i,0),0)*255.0);
        vec2 center=vec2(packed.r*256.0+packed.g,packed.b*256.0+packed.a)/65535.0*16.0-8.0;
        vec2 delta=q-center;
        if(abs(delta.x)>1.5 || abs(delta.y)>1.5) continue;
        float angle=shape.b*6.2831853;
        vec2 axis=vec2(cos(angle),-sin(angle));
        vec2 local=vec2(dot(delta,axis),dot(delta,vec2(-axis.y,axis.x)));
        float radius=shape.r,halfLength=shape.g;
        vec2 capsule=local-vec2(clamp(local.x,-halfLength,halfLength),0);
        float distance=length(capsule)-radius;
        if(kind==3) {
            vec2 box=abs(local)-vec2(halfLength,radius);
            distance=length(max(box,0.0))+min(max(box.x,box.y),0.0);
        }
        // Positive X faces the incoming train, which travels towards negative X.
        float hit=pow(max(sin(phase),0.0),4.0);
        float age=fract((phase-1.5707963)/6.2831853);
        float residue=exp(-age*5.0);
        float facing=.55+.45*clamp(delta.x/max(length(delta),.01),-1.0,1.0);
        float rim=exp(-square((distance-.045)/(.045+.075*hit)));
        float fragments=smoothstep(.29,.62,grain+noise(q*7.0-vec2(sin(t*.5)*2.0,0))*.15);
        float wake=exp(-square(distance/.23))*smoothstep(.05,.50,-delta.x)*residue;
        foam=max(foam,(rim*(hit*facing*1.7+residue*.30)+wake*.45)*fragments);
    }
    return foam;
}
#endif
void main() {
    float time=p.data[0],t=time*p.data[1];
#ifndef INTERACTION
    t=shoreWrappedTime(t);
#endif
    vec2 q=world.xz;
    vec2 shoreData=shore;
#ifndef INTERACTION
    vec3 boundaryData=shoreDistanceGradient(q);
    shoreData.x=boundaryData.x;
    float coast=(q.x+3.8-.7*sin(q.y*.33)-.22*sin(q.y*.91))*.96;
    float bedNoise=sin(q.x*1.37+q.y*.91)*.5+sin(q.x*3.17-q.y*2.23)*.25;
    shoreData.y=coast<0.0?coast*.19-.018*bedNoise:.04+coast*.115-.016*bedNoise;
    if(shoreData.x<-.24) discard;
#endif
    // Simulated mesh normals remain authoritative in the interactive scene.
    vec2 fine=vec2(sin(dot(q,vec2(7.1,2.3))+t*1.4),cos(dot(q,vec2(-3.2,8.5))-t));
    fine+=vec2(cos(dot(q,vec2(14.2,-6.1))-t*1.8),sin(dot(q,vec2(8.7,13.1))+t*.7))*.35;
    float detailScale=.028;
#ifdef INTERACTION
    detailScale=0.0;
#endif
    vec3 surfaceNormal=normal;
#ifdef INTERACTION
    // The solver normal is authoritative for this variant.
#endif
#ifndef INTERACTION
    vec2 distanceGradient=boundaryData.yz;
    // The full continuous wave drives shading even beyond the displacement LOD.
    // Coarse distant geometry stays flat, while its normal waves keep moving.
    // Filter sub-pixel slopes instead of switching the animation off at a world boundary.
    float footprint=max(length(dFdx(q)),length(dFdy(q)));
    float normalAmplitude=p.data[2]*(1.0-smoothstep(.35,1.4,footprint));
    float eps=.015;
    float hx=shoreWaveHeight(shoreData.x+eps*distanceGradient.x,q+vec2(eps,0),t,normalAmplitude)
        -shoreWaveHeight(shoreData.x-eps*distanceGradient.x,q-vec2(eps,0),t,normalAmplitude);
    float hz=shoreWaveHeight(shoreData.x+eps*distanceGradient.y,q+vec2(0,eps),t,normalAmplitude)
        -shoreWaveHeight(shoreData.x-eps*distanceGradient.y,q-vec2(0,eps),t,normalAmplitude);
    surfaceNormal=normalize(vec3(-hx/(2*eps),1,-hz/(2*eps)));
#endif
    vec3 N=normalize(surfaceNormal+vec3(fine.x*detailScale,0,fine.y*detailScale));
    vec3 V=normalize(camera-world),L=normalize(frame.lightDirIntensity.xyz);
    float ndv=clamp(dot(N,V),.001,1.0);
    vec2 size=vec2(textureSize(sceneColor,0));
    vec2 uv=gl_FragCoord.xy/max(size,vec2(1));
    float depth=max(shoreData.y,0.0);
#ifdef INTERACTION
    vec2 fieldUV=clamp(world.xz/12.8,vec2(0.0),vec2(1.0));
    vec4 field=texture(physicalField,fieldUV);
    depth=field.r*2.5;
    if(depth<0.004) discard;
    vec2 flow=(field.gb*2.0-1.0)*1.5;
    float flowSpeed=max(0.0,length(flow)-0.009);
#endif
    float depthRange=max(abs(p.data[16]),.01);
    float absorb=1-exp(-depth/depthRange*1.6);
    vec3 deep=vec3(p.data[10],p.data[11],p.data[12]);
    vec3 shallow=vec3(p.data[13],p.data[14],p.data[15]);
    vec3 water=mix(shallow,deep,absorb);
    vec2 bend=N.xz*p.data[28]*min(depth,.8);
#ifndef INTERACTION
    // Use camera-space slopes and a two-pixel footprint. Screen-color targets
    // may use a point sampler; explicit bilinear reconstruction keeps refraction
    // continuous as a wave sweeps across their texels.
    bend=(mat3(frame.view)*(N-vec3(0,1,0))).xy*p.data[28]*min(depth,.8);
    bend=clamp(bend,-vec2(2.0)/size,vec2(2.0)/size);
#endif
    vec2 distorted=clamp(uv+bend,vec2(.002),vec2(.998));
    // Reject a foreground sample displaced across the silhouette of a leg/rock.
    float n=max(frame.clipInfo.x,.001),f=max(frame.clipInfo.y,n+.1);
    float surface=clamp((-viewPosition.z-n)/(f-n),0,1);
    if(p.data[29]>=0) {
        float opaqueDepth=textureLod(sceneDepth,distorted,0.0).r;
        if(opaqueDepth<surface-.0001) distorted=uv;
    }
    #ifdef INTERACTION
    vec3 behind=textureLod(sceneColor,distorted,0.0).rgb;
#else
    vec3 behind=sampleSceneLinear(distorted);
#endif
    float caustic=pow(max(0.0,1.0-abs(sin(q.x*5.1+sin(q.y*3.7+t*.25))+sin(q.y*5.3-t*.3))*.5),12.0);
    behind+=vec3(.045,.065,.05)*caustic*p.data[26]*exp(-depth*1.3);
    float opacity=p.data[29]>=0?p.data[29]:-p.data[29]-1.0;
    float opticalDepth=depth/depthRange*4.5*opacity;
    vec3 transmission=exp(-vec3(2.0,.80,.45)*opticalDepth);
#ifndef INTERACTION
    // The opaque seabed ends at X=22 and |Z|=23. Outside it sceneColor
    // contains sky, not a submerged bed: never transmit/refract that horizon.
    float bedCoverage=(1.0-smoothstep(16.0,22.0,q.x))
        *(1.0-smoothstep(16.0,22.0,abs(q.y)));
    // At grazing angles the screen sample can cross the finite seabed
    // silhouette even when the current water point is inside its footprint.
    transmission*=bedCoverage*smoothstep(.25,.55,ndv);
#endif
    vec3 color=behind*transmission+water*(1-transmission);
    vec3 reflectedDir=reflect(-V,N);
    vec3 reflected=textureLod(environment,reflectedDir,1.65).rgb*frame.lightColor.w;
    vec3 sky=mix(vec3(.43,.55,.65),vec3(.075,.20,.38),sqrt(max(reflectedDir.y,0)));
    reflected=mix(reflected,sky,.68);
    // Schlick's water Fresnel, keeping the sand readable when viewed from above.
    float fresnel=.0204+.9796*pow(1-ndv,max(p.data[25],1.0));
    float reflectionWeight=clamp(fresnel*p.data[20],0,.92);
    color=mix(color,reflected,reflectionWeight);
#ifdef INTERACTION
    // The normal field carries the simulated contact waves; directional sky lighting
    // makes their opposing slopes readable without painting white rings onto the water.
    float rippleLight=dot(N.xz,normalize(vec2(-.6,-.8)));
    color*=clamp(1.0+rippleLight*1.6,.72,1.30);
#endif
    float exponent=1200.0;
    float variance=dot(dFdx(N),dFdx(N))+dot(dFdy(N),dFdy(N));
    float filtered=exponent/(1+exponent*variance);
    float sun=pow(max(dot(N,normalize(L+V)),0),filtered)*filtered/exponent;
    #ifdef INTERACTION
    color+=frame.lightColor.rgb*sun*p.data[24]*.20;
#else
    color+=frame.lightColor.rgb*sun*p.data[24]*.09;
#endif
    #ifdef INTERACTION
    float grain=foamGrain(q*19.0+vec2(-t*.24,t*.12));
#else
    float grain=foamGrain(q*19.0+vec2(-sin(t*.24),cos(t*.12))*.8);
#endif
    float phase=shoreWavePhase(shoreData.x,q,t);
    float crest=pow(max(sin(phase),0.0),10.0);
    float cycle=fract((phase-1.5707963)/6.2831853);
    float trail=exp(-cycle*7.0);
    float shoreEnvelope=(1-smoothstep(.7,2.6,shoreData.x))*smoothstep(-.05,.22,shoreData.x);
    float broken=smoothstep(.24,.61,grain+noise(q*3.0)*.16);
    // A dense broken leading lip, followed by a sparse, dissolving foam sheet.
    float lip=crest*shoreEnvelope*broken;
    float sheet=trail*shoreEnvelope*smoothstep(.44,.72,grain)*.60;
    float swash=.10*sin(t*1.35+sin(q.y*.8));
    float contact=exp(-square((shoreData.x-.09-swash)/.13));
    float foam=(lip*1.12+sheet*.55+contact*broken*.65)*p.data[9];
#ifndef INTERACTION
    foam=max(foam,woodContactFoam(q,t,grain,phase)*p.data[9]);
#endif
#ifdef INTERACTION
    // Breaker proxy from real shallow flow and converging velocity, never a clocked shore wave.
    vec2 texel=1.0/vec2(textureSize(physicalField,0));
    vec2 uL=(texture(physicalField,fieldUV-vec2(texel.x,0)).gb*2.0-1.0)*1.5;
    vec2 uR=(texture(physicalField,fieldUV+vec2(texel.x,0)).gb*2.0-1.0)*1.5;
    vec2 uD=(texture(physicalField,fieldUV-vec2(0,texel.y)).gb*2.0-1.0)*1.5;
    vec2 uU=(texture(physicalField,fieldUV+vec2(0,texel.y)).gb*2.0-1.0)*1.5;
    float compression=max(0.0,-(uR.x-uL.x+uU.y-uD.y)/0.2-0.12);
    float froude=flowSpeed/sqrt(9.81*max(depth,0.012));
    float shallowBand=1.0-smoothstep(0.025,0.15,depth);
    float activity=smoothstep(0.025,0.16,flowSpeed);
    float breaking=clamp(compression*0.55+froude*0.8+length(normal.xz)*2.0,0,1);
    float flowGrain=foamGrain(q*22.0-flow*time*0.4);
    foam=shallowBand*activity*breaking*smoothstep(.24,.58,flowGrain)*p.data[9]*0.65;
#endif
#ifdef INTERACTION
    float edge=smoothstep(.004,.018,depth);
#else
    float edge=smoothstep(-.06-swash,.10-swash,shoreData.x);
    color*=clamp(1.0+dot(N.xz,vec2(-.7,-.7))*1.15,.72,1.28);
#endif
    color=mix(color,vec3(p.data[17],p.data[18],p.data[19]),clamp(foam,0,.92));
    if(p.data[27]>0) {
        vec4 refl=texture(externalReflection,uv);
        color=mix(color,refl.rgb,clamp(refl.a*p.data[27],0,.8));
    }
    // Scene color is already composed here; blend once only at the dry edge.
    outColor=vec4(color*tint.rgb,edge*tint.a);
}
