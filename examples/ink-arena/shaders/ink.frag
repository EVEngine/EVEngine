#version 450

layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(set=0,binding=1) uniform sampler2D splatField;
layout(location=0) out vec4 outColor;

layout(push_constant) uniform InkStyle {
    float edgeStrength;
    float reflection;
    float roughness;
    float textureScale;
} style;

const float PI = 3.14159265;

// Canvas samplers are nearest by default; explicitly filter the field so the
// material has the same smooth contours as the previous uploaded texture.
vec4 sampleField(vec2 uv) {
    ivec2 size=textureSize(splatField,0);
    vec2 pixel=clamp(uv*vec2(size)-0.5,vec2(0),vec2(size-1));
    ivec2 a=ivec2(floor(pixel)), b=min(a+1,size-1);
    vec2 f=fract(pixel);
    return mix(mix(texelFetch(splatField,a,0),texelFetch(splatField,ivec2(b.x,a.y),0),f.x),
               mix(texelFetch(splatField,ivec2(a.x,b.y),0),texelFetch(splatField,b,0),f.x),f.y);
}

// The reference's tangentless frame: reconstruct UV directions on any face.
mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv) {
    vec3 dp1=dFdx(p), dp2=dFdy(p);
    vec2 duv1=dFdx(uv), duv2=dFdy(uv);
    vec3 a=cross(dp2,n), b=cross(n,dp1);
    vec3 t=a*duv1.x+b*duv2.x, bt=a*duv1.y+b*duv2.y;
    return mat3(normalize(t),normalize(bt),n);
}

// Dielectric GGX. Both the raised boundary and the wet interior react to view.
vec3 lightBRDF(vec3 n,vec3 v,vec3 l,vec3 albedo,float rough,vec3 radiance) {
    vec3 h=normalize(v+l);
    float nl=max(dot(n,l),0.0), nv=max(dot(n,v),0.001);
    float nh=max(dot(n,h),0.0), vh=max(dot(v,h),0.0);
    float a=rough*rough, a2=a*a;
    float denom=nh*nh*(a2-1.0)+1.0;
    float D=a2/max(PI*denom*denom,1e-6);
    float k=(rough+1.0)*(rough+1.0)/8.0;
    float G=(nl/(nl*(1.0-k)+k))*(nv/(nv*(1.0-k)+k));
    vec3 F=vec3(0.04)+(1.0-vec3(0.04))*pow(1.0-vh,5.0);
    return ((1.0-F)*albedo/PI+style.reflection*D*G*F/max(4.0*nl*nv,0.001))*radiance*nl;
}

void main() {
    vec4 field=sampleField(vUV);
    vec2 texel=1.0/vec2(textureSize(splatField,0));
    // Reference _ClipValue=.5, _ClipSoft=.01. Derivatives keep distant edges stable.
    float soft=max(0.01,0.65*fwidth(field.a));
    float mask=smoothstep(0.5-soft,0.5+soft,field.a);
    float inside=smoothstep(0.2,0.8,field.a);
    // Clamp every lookup to texel centres: UV boundaries are not ink contours.
    vec2 lo=texel*0.5, hi=1.0-lo;
    vec2 left=clamp(vUV-vec2(texel.x,0),lo,hi);
    vec2 right=clamp(vUV+vec2(texel.x,0),lo,hi);
    vec2 down=clamp(vUV-vec2(0,texel.y),lo,hi);
    vec2 up=clamp(vUV+vec2(0,texel.y),lo,hi);
    vec2 gradient=vec2(
        (sampleField(right).a-sampleField(left).a)/max(right.x-left.x,1e-6),
        (sampleField(up).a-sampleField(down).a)/max(up.y-down.y,1e-6));
    // Correct UV aspect ratio using the world-space length of each UV axis.
    vec3 dpX=dFdx(vWorldPos), dpY=dFdy(vWorldPos);
    vec2 uvX=dFdx(vUV), uvY=dFdy(vUV);
    float det=uvX.x*uvY.y-uvX.y*uvY.x;
    vec3 worldU=(dpX*uvY.y-dpY*uvX.y)/det;
    vec3 worldV=(dpY*uvX.x-dpX*uvY.x)/det;
    gradient/=max(vec2(length(worldU),length(worldV)),vec2(1e-6));
    vec2 edge=gradient*style.edgeStrength*(1.0-inside);
    edge/=max(1.0,length(edge)/0.45);
    vec3 n0=normalize(vNormal), v=normalize(vCameraPos-vWorldPos);
    mat3 tbn=cotangentFrame(n0,vWorldPos,vUV);
    vec3 detailPos=vWorldPos*style.textureScale;
    vec2 ripple=vec2(sin(detailPos.x*16.0+sin(detailPos.z*9.0)),
                      cos(detailPos.z*17.0+detailPos.y*13.0))*0.0025;
    vec2 offset=(-edge+ripple)*mask;
    vec3 n=normalize(tbn*vec3(offset,sqrt(max(0.01,1.0-dot(offset,offset)))));

    // Neutral matte tiles stay visible only where there is no ink.
    vec3 an=abs(n0);
    vec2 tileUV=an.y>0.7?vWorldPos.xz:(an.x>0.7?vWorldPos.zy:vWorldPos.xy);
    tileUV*=style.textureScale;
    vec2 cell=abs(fract(tileUV)-0.5);
    vec2 fw=fwidth(tileUV);
    float grout=max(smoothstep(0.486-fw.x,0.495+fw.x,cell.x),
                    smoothstep(0.486-fw.y,0.495+fw.y,cell.y));
    vec3 base=mix(vec3(0.38,0.41,0.44),vec3(0.14,0.16,0.18),grout);
    // The accumulated RGB is premultiplied by coverage. Decode team weight
    // before choosing a colour; otherwise thin ink darkens and mixed areas muddy.
    vec3 ink=field.rgb/max(field.a,1e-5);
    float violet=clamp((ink.b-0.015)/(0.95-0.015),0.0,1.0);
    float teamSoft=max(0.015,fwidth(violet)*0.65);
    float team=smoothstep(0.5-teamSoft,0.5+teamSoft,violet);
    vec3 inkColor=mix(vec3(1.0,0.22,0.015),vec3(0.24,0.035,0.95),team);
    vec3 albedo=mix(base,inkColor,mask);
    float rough=mix(0.88,style.roughness,mask);
    vec3 lit=albedo*vec3(0.24,0.28,0.34);
    lit+=lightBRDF(n,v,normalize(vec3(-0.5,1.0,-0.6)),albedo,rough,vec3(3.4,3.3,3.1));
    lit+=lightBRDF(n,v,normalize(vec3(0.8,0.5,-0.65)),albedo,rough,vec3(0.65,0.8,1.1));
    // Analytic studio sky reflection: broad softbox, no baked-on white streaks.
    vec3 reflected=reflect(-v,n);
    float sky=max(reflected.y,0.0);
    float softbox=pow(max(dot(reflected,normalize(vec3(-0.5,0.9,-0.7))),0.0),38.0);
    float fresnel=0.04+0.96*pow(1.0-max(dot(n,v),0.0),5.0);
    lit+=style.reflection*mask*fresnel*(vec3(0.28,0.36,0.48)*sky+vec3(8.0)*softbox);
    lit=lit/(lit+vec3(1.0));
    outColor=vec4(pow(lit,vec3(1.0/2.2)),1.0);
}
