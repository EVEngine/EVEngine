#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
struct Light3D{vec4 posRadius;vec4 color;};
layout(set=0,binding=0,std140) uniform Frame{mat4 mvp;mat4 model;vec4 lightDirIntensity;vec4 lightColor;vec4 tint;vec4 cameraPos;vec4 ambient;Light3D lights[8];vec4 texBomb;vec4 parallax;mat4 view;vec4 clipInfo;vec4 cloud;vec4 cloudWind;vec4 bindlessEnv;vec4 envProbeCenter;vec4 envProbeExtent;vec4 skinInfo;vec4 reflectionProbeCenter[2];vec4 reflectionProbeExtent[2];}ubo;
layout(set=0,binding=1) uniform sampler2D oceanAtlas;
layout(set=0,binding=6) uniform sampler2D wakeAtlas;
layout(push_constant) uniform Params { float data[32]; } p;
layout(location=0) out vec3 normal;
layout(location=1) out vec2 surface;
layout(location=2) out vec3 world;
vec4 weights(float t) {
 float t2=t*t,t3=t2*t;
 return vec4(-0.5*t+t2-0.5*t3,1-2.5*t2+1.5*t3,0.5*t+2*t2-1.5*t3,-0.5*t2+0.5*t3);
}
vec4 derivatives(float t) {
 float t2=t*t;
 return vec4(-0.5+2*t-1.5*t2,-5*t+4.5*t2,0.5+4*t-4.5*t2,-t+1.5*t2);
}
float decode(vec4 sampleValue) {
 uvec4 bytes=uvec4(round(sampleValue*255.0));
 return uintBitsToFloat(bytes.x|(bytes.y<<8)|(bytes.z<<16)|(bytes.w<<24));
}
vec3 displacement(ivec2 point,int n,int row) {
 return vec3(decode(texelFetch(oceanAtlas,ivec2(point.x,point.y+row),0)),
             decode(texelFetch(oceanAtlas,ivec2(point.x,point.y+row+n),0)),
             decode(texelFetch(oceanAtlas,ivec2(point.x,point.y+row+2*n),0)));
}
vec2 edge(float coordinate,float n,float cell,float width) {
 float farDistance=n-1-coordinate;
 float t=clamp(min(coordinate,farDistance)*cell/width,0,1);
 return vec2(t*t*(3-2*t),(coordinate<farDistance?1:-1)*6*t*(1-t)/width);
}
void main() {
 vec3 delta=vec3(0),dx=vec3(1,0,0),dz=vec3(0,0,1);
 for(int band=0;band<int(p.data[20]);++band) {
  int offset=band*5,n=int(p.data[offset]),row=int(p.data[offset+2]);
  float length=p.data[offset+1],scale=float(n)/length;
  vec2 uv=fract((inPos.xz+vec2(p.data[offset+3],p.data[offset+4]))/length)*float(n);
  ivec2 base=ivec2(floor(uv));vec2 f=fract(uv);
  vec4 wx=weights(f.x),wz=weights(f.y),gx=derivatives(f.x),gz=derivatives(f.y);
  for(int j=0;j<4;++j) for(int i=0;i<4;++i) {
   ivec2 point=(base+ivec2(i-1,j-1)+ivec2(n))%n;
   vec3 value=displacement(point,n,row);
   delta+=value*wx[i]*wz[j];dx+=value*gx[i]*wz[j]*scale;dz+=value*wx[i]*gz[j]*scale;
  }
 }
 float foam=0;
 int n=int(p.data[24]);
 if(n>0) {
  float length=p.data[25],scale=float(n)/length;
  vec2 uv=((inPos.xz+delta.xz+vec2(p.data[27],p.data[28]))/length+0.5)*float(n);
  if(all(greaterThanEqual(uv,vec2(0)))&&all(lessThan(uv,vec2(n-1)))) {
   ivec2 base=ivec2(floor(uv));vec2 f=fract(uv),gradient=vec2(0);float height=0;
   vec4 wx=weights(f.x),wz=weights(f.y),gx=derivatives(f.x),gz=derivatives(f.y);
   for(int j=0;j<4;++j) for(int i=0;i<4;++i) {
    ivec2 point=base+ivec2(i-1,j-1);
    if(any(lessThan(point,ivec2(0)))||any(greaterThanEqual(point,ivec2(n))))continue;
    float h=decode(texelFetch(wakeAtlas,point,0));
    height+=h*wx[i]*wz[j];foam+=decode(texelFetch(wakeAtlas,point+ivec2(0,n),0))*wx[i]*wz[j];
    gradient+=h*vec2(gx[i]*wz[j],wx[i]*gz[j])*scale;
   }
   vec2 bx=edge(uv.x,float(n),length/float(n),p.data[26]);
   vec2 bz=edge(uv.y,float(n),length/float(n),p.data[26]);
   float blend=bx.x*bz.x;
   gradient=gradient*blend+height*vec2(bx.y*bz.x,bx.x*bz.y);
   delta.y+=height*blend;foam=clamp(foam*blend,0,1);
   dx.y+=dot(gradient,dx.xz);dz.y+=dot(gradient,dz.xz);
  }
 }
 vec3 local=inPos+vec3(p.data[21],p.data[22],p.data[23])+delta;
 gl_Position=ubo.mvp*vec4(local,1);
 world=(ubo.model*vec4(local,1)).xyz;
 normal=normalize(mat3(ubo.model)*normalize(cross(dz,dx)));
 surface=vec2(foam,dx.x*dz.z-dz.x*dx.z);
}
