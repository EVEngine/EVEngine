#version 450
layout(location=0) in vec3 position;
layout(location=0) out vec2 ndc;
layout(push_constant) uniform Parameters { vec4 p[8]; } parameters;
void main() {
    vec2 corner = position.xy * 0.5 + 0.5;
    ndc = mix(parameters.p[3].xy, parameters.p[3].zw, corner);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
