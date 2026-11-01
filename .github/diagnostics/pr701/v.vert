#version 450
#ifndef GENERATED
layout(location=0) in vec3 p;
#endif
#ifdef PUSH
layout(push_constant) uniform P { mat4 m; vec4 rows[3]; vec4 color; } push;
#endif
#ifdef UBO
layout(set=0,binding=0,std140) uniform U { mat4 m; vec4 color; } u;
#endif
#ifdef SSBO
layout(set=0,binding=21,std430) readonly buffer B { mat4 m; } b;
#endif
void main() {
#ifdef GENERATED
vec2 v[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
gl_Position=vec4(v[gl_VertexIndex],0.5,1);
#else
gl_Position=vec4(p,1);
#endif
#ifdef PUSH
gl_Position=push.m*gl_Position;
#endif
#ifdef UBO
gl_Position=u.m*gl_Position;
#endif
#ifdef SSBO
gl_Position=b.m*gl_Position;
#endif
}
