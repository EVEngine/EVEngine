#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(set=0,binding=0) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint; vec4 cameraPos;
} ubo;
layout(push_constant) uniform Params {float p[32];} u;
layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(location=2) out vec4 vTint;
layout(location=3) out vec3 vLightDir;
layout(location=4) out vec3 vLightColor;
layout(location=5) out vec3 vWorldPos;
layout(location=6) out vec3 vCameraPos;
layout(location=7) out vec3 vLocalPos;
layout(location=8) out vec3 vLocalNormal;
float hash(vec3 p) {return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
void main() {
    vec3 pos=inPos;
    vLocalPos=inPos;vLocalNormal=inNormal;
    if(u.p[27]>1.5 && u.p[27]<2.5) {
        // Authored cube vertices stay inside their original shared unit-space cell.
        vec3 cell=floor(inPos/0.125), center=(cell+0.5)*0.125;
        float phase=u.p[30]>0.5?u.p[14]:fract(u.p[0]*u.p[1]/max(u.p[28],0.01));
        float boundary=mix(0.25,3.65,phase), id=hash(cell+u.p[13]);
        float distance=center.y-boundary;
        if(abs(distance)>0.38 || id<0.35 || phase<0.025 || phase>0.985) {
            gl_Position=vec4(2,2,2,1);return;
        }
        float age=clamp(-distance+0.15,0.0,0.6);
        vec3 direction=normalize(vec3(center.x,0.2,center.z)+vec3(0.0001));
        pos+=direction*age*0.85+vec3(0,age*0.8,0);
        pos.x+=sin(id*40.0+u.p[0]*3.0)*age*0.18;
    }
    if(u.p[27]>2.5 && u.p[27]<3.5) {
        float age=mod(u.p[0]*u.p[1],max(u.p[28],0.01));
        pos=vec3(u.p[23],u.p[24],u.p[25]+0.055)+vec3(inPos.xy*(0.08+age*8.0),0);
        if(age>0.38) {gl_Position=vec4(2,2,2,1);return;}
    }
    if(u.p[27]>3.5) {
        float t=u.p[0]*u.p[1];
        int mode=int(u.p[10]+0.5);
        if(mode==14) {
            pos.x+=sin(inPos.y*5.0+inPos.z*4.0-t*1.4)*0.10;
            pos.z+=cos(inPos.y*3.0+inPos.x*2.0-t)*0.08;
            pos.y+=sin(inPos.y*2.0-t)*0.05;
        } else if(mode==5 || mode==24) {
            float band=floor(inPos.z+0.5);
            float height=mode==5?0.45+mod(t*0.65+band*1.1,3.1):0.75+band*0.9+sin(t*0.7+band)*0.08;
            float radius=mode==5?1.42:1.55;
            pos=vec3(inPos.x*radius,height+(inPos.z-band)+inPos.x*sin(t*0.4+band)*0.08,inPos.y*0.7);
        } else {
            vec3 cell=floor(inPos/0.125), center=(cell+0.5)*0.125;
            vec3 local=inPos-center;
            float id=hash(cell+u.p[13]);
            if(mode==2) {
                if(id<0.79 || center.y<0.8 || center.y>2.8 || (abs(center.x)<0.58 && center.y<2.6)) {
                    gl_Position=vec4(2,2,2,1);return;
                }
                float top=step(0.0,local.y);
                vec3 outward=normalize(vec3(center.x*0.35,1.0,center.z*0.2));
                pos=center+vec3(local.x,0,local.z)*(1.0-top)*0.8+
                    outward*(top*(0.03+id*0.07)-0.03);
            } else if(mode==17) {
                if(id<0.88) {gl_Position=vec4(2,2,2,1);return;}
                center.y=0.4+mod(center.y-t*(0.7+id*0.5),3.3);
                center.x*=1.08;center.z+=0.11;
                pos=center+local*vec3(0.12,1.1,0.12);
            }
        }
    }
    gl_Position=ubo.mvp*vec4(pos,1.0);
    vWorldPos=(ubo.model*vec4(pos,1.0)).xyz;
    vNormal=normalize(transpose(inverse(mat3(ubo.model)))*inNormal);
    vUV=inUV;vTint=ubo.tint;
    vLightDir=normalize(ubo.lightDirIntensity.xyz);
    vLightColor=ubo.lightColor.rgb*max(ubo.lightDirIntensity.w,0.0);
    vCameraPos=ubo.cameraPos.xyz;
}
