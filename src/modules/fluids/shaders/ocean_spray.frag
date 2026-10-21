#version 450
layout(location=0) in vec2 uv;
layout(location=1) in float coverage;
layout(location=0) out vec4 color;
void main() {
    float alpha=(1.0-smoothstep(0.2,1.0,length(uv)))*coverage*0.7;
    color=vec4(0.85,0.94,1.0,alpha);
}
