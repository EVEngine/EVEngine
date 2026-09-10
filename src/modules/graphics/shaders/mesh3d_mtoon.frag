#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor;
    vec4 tint; vec4 cameraPos; vec4 ambient; Light3D lights[8];
    vec4 texBomb; vec4 parallax; mat4 view;
} frame;
layout(set=0,binding=1) uniform sampler2D atlas;
layout(set=0,binding=6) uniform sampler2D metadata;
layout(set=0,binding=3) uniform samplerCube environment;
layout(push_constant) uniform Externals { float data[32]; } u;
layout(location=0) out vec4 outColor;
float word(int i) { uvec4 b=uvec4(round(texelFetch(metadata,ivec2(i,0),0)*255)); return uintBitsToFloat(b.x|(b.y<<8)|(b.z<<16)|(b.w<<24)); }
vec4 info(int i) { return vec4(word(i*4),word(i*4+1),word(i*4+2),word(i*4+3)); }
vec3 linearColor(vec3 c) { return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c)); }
int wrapped(int n,int size,int mode) {
    if(mode==33071) return clamp(n,0,size-1);
    int period=mode==33648?2*size:size;
    n=((n%period)+period)%period;
    return n>=size?2*size-1-n:n;
}
vec4 sampleSlot(int slot,vec2 uv,vec4 missing) {
    vec4 rect=info(5+slot*3), transform=info(6+slot*3), params=info(7+slot*3);
    if(params.w<.5) return missing;
    float c=cos(params.x),s=sin(params.x);
    uv=mat2(c,s,-s,c)*(uv*transform.zw)+transform.xy;
    vec2 p=uv*rect.zw-.5; ivec2 base=ivec2(floor(p)); vec2 f=fract(p);
    ivec2 size=ivec2(rect.zw), origin=ivec2(rect.xy);
    ivec2 a=ivec2(wrapped(base.x,size.x,int(params.y)),wrapped(base.y,size.y,int(params.z)));
    ivec2 b=ivec2(wrapped(base.x+1,size.x,int(params.y)),wrapped(base.y+1,size.y,int(params.z)));
    return mix(mix(texelFetch(atlas,origin+a,0),texelFetch(atlas,origin+ivec2(b.x,a.y),0),f.x),
               mix(texelFetch(atlas,origin+ivec2(a.x,b.y),0),texelFetch(atlas,origin+b,0),f.x),f.y);
}
vec3 pushed(int n) { return vec3(u.data[n],u.data[n+1],u.data[n+2]); }
layout(set=0,binding=4,std140) uniform ShadowFrame {
    mat4 lightVP[3]; vec4 splits; vec4 bias; vec4 cascadeBias; vec4 cascadeTexel;
} shadow;
layout(set=0,binding=5) uniform sampler2DArrayShadow shadowMap;
float visibility() {
    if(shadow.bias.y<.5 || shadow.bias.z<.5 || shadow.splits.w<=0) return 1;
    float depth=-vViewPos.z;
    int cascade=depth<shadow.splits.x?0:depth<shadow.splits.y?1:2;
    vec4 clip=shadow.lightVP[cascade]*vec4(vWorldPos,1);
    vec3 p=clip.xyz/max(clip.w,1e-6); p.xy=p.xy*.5+.5;
    if(any(lessThan(p,vec3(0))) || any(greaterThan(p,vec3(1)))) return 1;
    float bias=max(shadow.cascadeBias[cascade],shadow.bias.x);
    vec2 texel=1.0/vec2(textureSize(shadowMap,0).xy);
    float sum=0;
    for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y)
        sum+=texture(shadowMap,vec4(p.xy+vec2(x,y)*texel,float(cascade),p.z-bias));
    return mix(1,sum/9,clamp(shadow.splits.w,0,1));
}
void main() {
    vec4 shape=info(2), settings=info(4), params=info(0), rimParams=info(1), animation=info(3);
    bool outline=u.data[24]>.5;
    bool front = gl_FrontFacing;
    if(outline ? front : (!front && settings.y<.5)) discard;
    vec2 uv=vUV*vec2(u.data[19],u.data[20])+vec2(u.data[21],u.data[22]);
    float mask=sampleSlot(8,uv,vec4(1)).b;
    float angle=animation.z*u.data[23]*mask;
    uv=mat2(cos(angle),sin(angle),-sin(angle),cos(angle))*(uv-.5)+.5;
    uv+=animation.xy*u.data[23]*mask;
    vec4 base=sampleSlot(0,uv,vec4(1));
    float alpha=base.a*u.data[3];
    if(shape.z==1 && alpha<shape.w) discard;
    if(shape.z!=2) alpha=1;
    vec3 litColor=linearColor(base.rgb)*pushed(0);
    vec3 N=normalize(vNormal)*(front?1.:-1.);
    if(info(13).w>.5) {
        vec3 mapN=sampleSlot(2,uv,vec4(.5,.5,1,1)).xyz*2.-1.; mapN.xy*=params.w;
        vec3 dp1=dFdx(vWorldPos),dp2=dFdy(vWorldPos); vec2 duv1=dFdx(uv),duv2=dFdy(uv);
        vec3 T=dp1*duv2.y-dp2*duv1.y, B=-dp1*duv2.x+dp2*duv1.x;
        float inv=inversesqrt(max(max(dot(T,T),dot(B,B)),1e-12));
        N=normalize(mat3(T*inv,B*inv,N)*mapN);
    }
    vec3 V=normalize(vCameraPos-vWorldPos);
    vec3 shade=linearColor(sampleSlot(1,uv,vec4(1)).rgb)*pushed(4);
    float shift=params.x+sampleSlot(3,uv,vec4(0)).r*animation.w;
    vec3 gi=frame.ambient.rgb;
    vec3 directionalGi=textureLod(environment,N,5).rgb;
    vec3 averageGi=(textureLod(environment,vec3(0,1,0),5).rgb+textureLod(environment,vec3(0,-1,0),5).rgb)*.5;
    gi+=mix(directionalGi,averageGi,params.z)*frame.lightColor.w;
    vec3 color=gi*litColor, lighting=gi;
    float shadowVisibility=visibility();
    for(int i=0;i<min(int(frame.lightDirIntensity.w+.5),8);++i) {
        vec3 L=frame.lights[i].posRadius.xyz; float attenuation=1;
        float radius=frame.lights[i].posRadius.w;
        if(radius>0) { L-=vWorldPos; float distance=length(L); attenuation=pow(max(1-distance/radius,0),2); }
        L=normalize(L); vec3 radiance=frame.lights[i].color.rgb*attenuation;
        float shading=clamp(((radius<=0?shadowVisibility:1)*(dot(N,L)+1)+shift-params.y)/max(2-2*params.y,1e-5),0,1);
        color+=mix(shade,litColor,shading)*radiance;
        lighting+=radiance;
    }
    if(settings.x<.5) color=litColor;
    vec3 viewX=normalize(abs(V.y)<.999?vec3(V.z,0,-V.x):vec3(1,0,0));
    vec3 viewY=cross(V,viewX);
    vec2 matcapUv=vec2(dot(viewX,N),-dot(viewY,N))*.495+.5;
    vec3 rim=linearColor(sampleSlot(5,matcapUv,vec4(0)).rgb)*pushed(10);
    rim+=pow(clamp(1-dot(N,V)+rimParams.z,0,1),max(rimParams.y,1e-5))*pushed(13);
    rim*=linearColor(sampleSlot(6,uv,vec4(1)).rgb)*mix(vec3(1),lighting,rimParams.x);
    color+=rim+linearColor(sampleSlot(4,uv,vec4(1)).rgb)*pushed(7)*settings.w;
    if(outline) color=pushed(16)*mix(vec3(1),lighting,shape.x);
    outColor=vec4(max(color,vec3(0)),alpha);
}
