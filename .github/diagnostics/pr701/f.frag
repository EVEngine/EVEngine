#version 450
layout(location=0) out vec4 color;
#ifdef MRT
layout(location=1) out vec4 c1; layout(location=2) out vec4 c2; layout(location=3) out vec4 c3; layout(location=4) out vec4 c4;
#endif
#ifdef PUSH
layout(push_constant) uniform P { mat4 m; vec4 rows[3]; vec4 color; } push;
#endif
#ifdef UBO
layout(set=0,binding=0,std140) uniform U { mat4 m; vec4 color; } u;
#endif
#if defined(SAMPLER_VERTEX) || defined(SAMPLER_ALL)
layout(set=0,binding=1) uniform sampler2D texture0;
#endif
void main() {
color=vec4(1,0,0,1);
#if defined(SAMPLER_VERTEX) || defined(SAMPLER_ALL)
color=texture(texture0,vec2(0.5));
#endif
#ifdef MRT
c1=color;c2=color;c3=color;c4=color;
#endif
#ifdef PUSH
color=push.color;
#endif
#ifdef UBO
color=u.color;
#endif
}
