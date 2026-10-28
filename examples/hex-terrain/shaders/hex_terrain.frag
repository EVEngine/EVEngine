#version 450

layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;

struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind;
} ubo;
layout(location=0) out vec4 outColor;

const float PI = 3.14159265359;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),
               mix(hash21(i+vec2(0,1)),hash21(i+vec2(1,1)),f.x),f.y);
}
float fbm(vec2 p) {
    float s=0.0,a=0.5;
    for(int i=0;i<5;++i){s+=noise(p)*a;p=mat2(1.6,-1.2,1.2,1.6)*p;a*=0.5;}
    return s;
}

struct Surface { vec3 albedo; float roughness; float metallic; float height; };
Surface biome(int id, vec2 p) {
    float fine=fbm(p*2.7), grain=noise(p*15.0);
    Surface s=Surface(vec3(0.3),0.85,0.0,fine);
    if(id==0) s=Surface(mix(vec3(0.008,0.045,0.12),vec3(0.018,0.13,0.24),fine),0.16,0.0,fine);
    else if(id==1) s=Surface(mix(vec3(0.012,0.16,0.29),vec3(0.035,0.38,0.48),fine),0.20,0.0,fine);
    else if(id==2) s=Surface(mix(vec3(0.46,0.36,0.16),vec3(0.74,0.63,0.31),grain),0.82,0.0,grain);
    else if(id==3) s=Surface(mix(vec3(0.12,0.34,0.045),vec3(0.46,0.58,0.10),fine),0.88,0.0,fine);
    else if(id==4) s=Surface(mix(vec3(0.25,0.31,0.08),vec3(0.50,0.43,0.18),grain),0.90,0.0,grain);
    else if(id==5) s=Surface(mix(vec3(0.20,0.21,0.23),vec3(0.62,0.60,0.56),fine),0.66,0.0,fine);
    else if(id==6) s=Surface(mix(vec3(0.018,0.17,0.035),vec3(0.08,0.38,0.055),fine),0.92,0.0,fine);
    else if(id==7) s=Surface(mix(vec3(0.045,0.09,0.03),vec3(0.19,0.24,0.045),fine),0.96,0.0,fine);
    else if(id==8) s=Surface(mix(vec3(0.005,0.14,0.035),vec3(0.025,0.42,0.065),fine),0.90,0.0,fine);
    else if(id==9) s=Surface(mix(vec3(0.58,0.78,0.88),vec3(0.98,1.0,1.0),fine),0.28,0.0,fine);
    else if(id==10) s=Surface(mix(vec3(0.18,0.13,0.095),vec3(0.50,0.43,0.34),grain),0.78,0.0,grain);
    else if(id==11) s=Surface(mix(vec3(0.018,0.26,0.38),vec3(0.08,0.62,0.67),fine),0.12,0.0,fine);
    return s;
}

void main() {
    int primary=int(floor(vUV.x+0.001));
    int secondary=int(floor(vUV.y+0.001));
    float time=ubo.cloud.z;
    float edgeBlend=fract(vUV.x);
    float river=secondary==11 ? fract(vUV.y) : 0.0;
    vec2 p=vWorldPos.xz*0.19;
    float breakup=smoothstep(0.28,0.72,fbm(p*1.3+17.0));
    float channelWave=abs(sin(p.x*3.1+p.y*2.35+fbm(p*2.0)*2.2));
    float riverChannel=(1.0-smoothstep(0.12+river*0.12,0.32+river*0.18,channelWave))
                       *smoothstep(0.18,0.50,river);
    float blend=secondary==11 ? riverChannel : edgeBlend*mix(0.45,1.10,breakup);
    blend=clamp(blend,0.0,1.0);
    Surface a=biome(primary,p), b=biome(secondary,p+31.7);
    vec3 albedo=mix(a.albedo,b.albedo,blend);
    float roughness=mix(a.roughness,b.roughness,blend);
    float metallic=mix(a.metallic,b.metallic,blend);
    float swampPuddle=0.0;
    if(primary==7) {
        float basin=fbm(p*1.75+vec2(8.3,-5.7));
        swampPuddle=smoothstep(0.58,0.72,basin)
                    *smoothstep(0.32,0.52,noise(p*8.0+4.1));
        albedo=mix(albedo,vec3(0.018,0.075,0.055),swampPuddle*0.82);
        roughness=mix(roughness,0.12,swampPuddle);
    } else if(primary==8) {
        float wetness=smoothstep(0.46,0.76,fbm(p*2.2+13.0));
        albedo*=mix(1.0,0.72,wetness*0.48);
        roughness=mix(roughness,0.54,wetness*0.62);
    } else if(primary==9) {
        float ridge=abs(noise(p*9.0)-noise(p*9.0+vec2(0.11,0.07)));
        float crack=smoothstep(0.055,0.095,ridge);
        albedo=mix(albedo,vec3(0.20,0.48,0.63),crack*0.72);
        roughness=mix(roughness,0.16,crack*0.58);
    }
    if(primary==5) {
        float snow=smoothstep(4.15,5.65,vWorldPos.y+fbm(p*1.7)*0.95);
        albedo=mix(albedo*vec3(0.62,0.65,0.70),vec3(0.88,0.94,0.98),snow);
        roughness=mix(roughness,0.34,snow);
    }

    bool water=primary<=1 || (secondary==11 && riverChannel>0.35);
    float h=fbm(p*5.0);
    if(water) h+=sin(vWorldPos.x*1.8+time*1.35)*0.22
                    +sin(vWorldPos.z*2.3-time*1.05)*0.18;
    float detailStrength=water ? 0.055 : (primary==5 || primary==10 ? 0.28 : 0.16);
    vec3 N=normalize(vNormal+vec3(dFdx(h),0.0,dFdy(h))*detailStrength);
    vec3 V=normalize(vCameraPos-vWorldPos);
    vec3 L=normalize(ubo.lightDirIntensity.xyz);
    vec3 H=normalize(V+L);
    float ndl=max(dot(N,L),0.0), ndv=max(dot(N,V),0.001), ndh=max(dot(N,H),0.0);
    float alpha=max(.025,roughness*roughness), a2=alpha*alpha;
    float D=a2/max(PI*pow(ndh*ndh*(a2-1.0)+1.0,2.0),.001);
    float k=pow(roughness+1.0,2.0)/8.0;
    float G=(ndl/(ndl*(1.0-k)+k))*(ndv/(ndv*(1.0-k)+k));
    vec3 F0=mix(vec3(.04),albedo,metallic);
    vec3 F=F0+(1.0-F0)*pow(1.0-max(dot(H,V),0.0),5.0);
    vec3 spec=D*G*F/max(4.0*ndl*ndv,.001);
    vec3 direct=((1.0-F)*(1.0-metallic)*albedo/PI+spec)*ndl*ubo.lightColor.rgb*1.75;
    float sky=max(0.0,N.y)*0.20+0.08;
    vec3 color=direct+albedo*(ubo.ambient.rgb+vec3(0.16,0.19,0.22)*sky);
    if(swampPuddle>0.0) {
        float puddleFresnel=pow(1.0-ndv,4.0);
        color=mix(color,vec3(0.035,0.16,0.15)+vec3(0.18,0.30,0.28)*puddleFresnel,
                  swampPuddle*0.48);
        color+=vec3(0.42,0.58,0.52)*pow(max(dot(N,H),0.0),52.0)*swampPuddle*0.45;
    }
    if(water) {
        float waterDepth=primary==0 ? 1.0 : (primary==1 ? 0.42 : 0.18);
        float grazingDepth=waterDepth/max(ndv,0.24);
        vec3 absorption=exp(-vec3(2.8,1.15,0.48)*grazingDepth);
        vec3 deepScatter=vec3(0.006,0.055,0.105);
        vec3 shallowScatter=vec3(0.025,0.31,0.39);
        vec3 volumeColor=mix(deepScatter,shallowScatter,exp(-waterDepth*2.35));
        float causticA=sin(vWorldPos.x*5.8+sin(vWorldPos.z*3.7+time*0.75));
        float causticB=sin(vWorldPos.z*6.4-sin(vWorldPos.x*4.1-time*0.58));
        float caustics=pow(clamp(causticA*causticB*0.5+0.5,0.0,1.0),5.0)
                       *exp(-waterDepth*4.2);
        color=color*absorption+volumeColor*(1.0-absorption);
        color+=vec3(0.12,0.48,0.46)*caustics*0.34;
        float fresnel=0.025+0.975*pow(1.0-ndv,5.0);
        vec3 skyReflection=mix(vec3(0.08,0.18,0.25),vec3(0.28,0.52,0.64),
                               max(N.y,0.0));
        color=mix(color,skyReflection,fresnel*0.72);
        color+=vec3(0.62,0.78,0.84)*pow(max(dot(N,H),0.0),64.0)*0.82;
    }
    if(primary==2) color+=vec3(0.16,0.12,0.045)*(1.0-breakup);
    bool shorePair=(primary<=1 && secondary==2)||(primary==2 && secondary<=1);
    if(shorePair && edgeBlend>0.75) {
        float crest=0.66+0.22*sin(vWorldPos.x*7.1+vWorldPos.z*5.3+time*2.2)
                         +0.12*sin(vWorldPos.x*13.7-vWorldPos.z*9.2-time*1.4);
        color=mix(color,vec3(0.82,0.94,0.96),clamp(crest,0.34,0.86));
    }
    float cloudField=fbm(vWorldPos.xz*0.055+ubo.cloudWind.xy*time*0.018);
    float cloudMask=smoothstep(ubo.cloudWind.z-0.14,ubo.cloudWind.z+0.14,cloudField);
    color*=1.0-ubo.cloud.x*cloudMask*0.28;
    color=1.0-exp(-color*1.22);
    color=pow(max(color,vec3(0.0)),vec3(1.0/2.2));
    float distanceFog=smoothstep(31.0,52.0,length(vViewPos));
    color=mix(color,vec3(0.075,0.11,0.17),distanceFog*0.48);
    outColor=vec4(color*vTint.rgb,1.0);
}
