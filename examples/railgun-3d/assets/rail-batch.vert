#version 450
// Immutable particle seeds are vertex attributes, not per-frame transforms.
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inSeed;
layout(location=2) in vec2 inUV;
struct Light3D { vec4 posRadius; vec4 color; vec4 spot; };
// Frame prefix matches graphics/shaders/mesh3d.vert (through view).
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor;
    vec4 tint; vec4 cameraPos; vec4 ambient; Light3D lights[8];
    vec4 texBomb; vec4 parallax; mat4 view;
} frame;
layout(push_constant) uniform Externals { float data[32]; } u;
layout(location=0) out vec2 vUV;
layout(location=1) out vec4 vColor;
void main() {
    float age=u.data[0];
    vec3 a=vec3(u.data[1],u.data[2],u.data[3]);
    vec3 axis=vec3(u.data[5],u.data[6],u.data[7]); float length=u.data[8];
    vec3 radialU=vec3(u.data[9],u.data[10],u.data[11]); float flight=u.data[12];
    vec3 radialV=vec3(u.data[13],u.data[14],u.data[15]);
    vec3 pos; vUV=inUV;
#if defined(BEAM)
    float s=inSeed.x, t=age-s/length*flight;
    float heat=clamp(t/u.data[16],0.0,1.0), energy=1.0-heat;
    float yellow=clamp(1.0-t/0.75,0.0,1.0);
    float radius=0.045+0.05*heat;
    pos=a+axis*(s+length/96.0+inPos.y*length/48.0)
        +radialU*inPos.x*radius+radialV*inPos.z*radius;
    vColor=vec4((0.4+9.0*energy)*energy,7.5*yellow*yellow+0.025*energy,
                1.5*yellow*yellow,energy*energy);
    if(t<0.0 || heat>=1.0) vColor.a=0.0;
#elif defined(SPARK)
    float s=inSeed.x, angle=inSeed.y, speed=inSeed.z;
    float t=age-s/length*flight;
    float f=clamp(t/inUV.x,0.0,1.0);
    pos=a+axis*s+radialU*(0.08*cos(angle))+radialV*(0.08*sin(angle));
    pos.x+=0.14*t*cos(angle)+0.18*(sin(angle+t*2.1)-sin(angle));
    pos.z+=0.16*t*sin(angle)+0.12*(cos(angle+t*1.7)-cos(angle));
    pos.y+=(0.045+0.025*speed)*t+0.003*t*t;
    pos+=inPos*inUV.y*(1.0+0.5*f);
    vColor=vec4(2.2-1.7*f,2.3-1.55*f,2.5-1.5*f,(1.0-f)*(1.0-f));
    if(t<0.0 || f>=1.0) vColor.a=0.0;
#else
    float s=inPos.x, theta=inPos.y, phase=inPos.z;
    float jitter=inSeed.x, speed=inSeed.y;
    float t=age-s/length*flight;
    float fade=clamp(1.0-t/4.3,0.0,1.0);
    float angle=theta+0.07*sin(phase+t*speed)*t;
    float radius=0.37+0.22*t+0.07*sin(phase+t*1.2)*t;
    pos=a+axis*(s+0.10*sin(phase+t)*t)+radialU*radius*cos(angle)+radialV*radius*sin(angle);
    pos.y+=0.035*t+0.004*t*t; pos.x+=0.07*t*sin(phase+t*0.8);
    // Same spherical facing frame as rgFace; preserve shape during orbit.
    vec3 forward=normalize(frame.cameraPos.xyz-pos);
    vec3 right=normalize(cross(vec3(0.0,1.0,0.0),forward));
    vec3 up=cross(forward,right);
    float size=0.42+0.14*jitter+(0.23+0.12*jitter)*t;
    pos+=((inUV.x-0.5)*right+(inUV.y-0.5)*up)*size;
    vColor=vec4(0.36,0.46,0.57,(0.28+0.08*jitter)*fade*fade);
    if(t<0.0 || fade<=0.0) vColor.a=0.0;
#endif
    gl_Position=vColor.a>0.00001 ? frame.mvp*vec4(pos,1.0) : vec4(2.0,2.0,2.0,1.0);
}
