#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;

struct Light3D { vec4 posRadius; vec4 color; };
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor; vec4 tint;
    vec4 cameraPos; vec4 ambient; Light3D lights[8]; vec4 texBomb; vec4 parallax;
    mat4 view; vec4 clipInfo; vec4 cloud; vec4 cloudWind; vec4 virtualTexture;
    vec4 virtualAtlas; vec4 bindlessEnv; vec4 envProbeCenter; vec4 envProbeExtent; vec4 skinInfo;
} ubo;
layout(push_constant) uniform Externals { float data[16]; } u;

layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(location=2) out vec4 vTint;
layout(location=3) out vec3 vWorldPos;
layout(location=4) out vec3 vCameraPos;
layout(location=5) out vec3 vViewPos;

vec3 deform(vec3 local, vec3 root) {
    float range=abs(u.data[5]), main=u.data[3];
    if(range==0.0 || main==0.0) return local;
    vec3 distance=(root-ubo.cameraPos.xyz)/range;
    float attenuation=1.0-clamp(dot(distance,distance),0.0,1.0);
    attenuation*=attenuation;
    vec2 dimensions=vec2(u.data[14],u.data[15]);
    vec3 flex=vec3(u.data[6],u.data[7],u.data[8])*
        vec3(clamp(main*3.0,0.0,1.0),clamp(main*2.0,0.0,1.0),1.0-main*main*0.5)*main*attenuation;
    vec3 frequency=vec3(u.data[9],u.data[10],u.data[11]);
    vec3 direction=vec3(u.data[0],u.data[1],u.data[2]);
    float time=-fract(u.data[4]*6.0)*6.283185;
    vec3 norm=local/vec3(dimensions.x,dimensions.y,dimensions.x);
    float branch=dot(norm.xz,norm.xz), stem=clamp(norm.y,0.0,1.0);
    float lengthA=dot(local,local);
    vec3 world=root+local;
    float gust=((sin(time+frequency.x*(root.x+root.y+root.z))*0.3+main*0.5)+
        (u.data[12]*0.4+main)*main)*(u.data[13]*0.3+0.7);
    vec3 tally=vec3(direction.x,0.0,direction.z)*stem*stem*gust*flex.x;
    gust=gust*0.7+0.3;
    if(u.data[5]>0.0) {
        vec3 displaced=world+tally*0.25;
        tally+=direction*stem*stem*(sin(time*2.0+dot(displaced,vec3(frequency.y)))*branch*0.7+0.3)*gust*flex.y;
    }
    vec3 offset=local+tally;
    float denominator=dot(offset,offset);
    float normalization=denominator==0.0?0.0:clamp(lengthA/denominator,0.0,1.0);
    tally=offset*normalization;
    if(u.data[5]>0.0 && flex.z!=0.0) {
        vec3 wave=sin(vec3(time*5.0)+(world+tally)*frequency.z);
        tally+=(wave*direction+direction)*vec3(branch,branch*0.75,branch)*(stem*gust*flex.z)*(normalization+0.5);
    }
    return tally;
}

void main() {
    vec3 root=ubo.model[3].xyz;
    mat3 model3=mat3(ubo.model);
    vec3 objectPos=inverse(model3)*deform(model3*inPos,root);
    vec4 world=ubo.model*vec4(objectPos,1.0);
    gl_Position=ubo.mvp*vec4(objectPos,1.0);
    vWorldPos=world.xyz; vViewPos=(ubo.view*world).xyz;
    vNormal=normalize(transpose(inverse(model3))*inNormal);
    vUV=inUV; vTint=ubo.tint; vCameraPos=ubo.cameraPos.xyz;
}
