#version 450
layout(location=0) in vec3 position;
layout(push_constant) uniform Exposure { float p[32]; } exposure;
void main() {
    gl_Position = vec4(position.xy * exposure.p[28], 0.0, 1.0);
}
