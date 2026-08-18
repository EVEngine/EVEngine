// Resolve: fullscreen triangle producing a shading pass over the visibility
// buffer. Provided as the production path for compositing into the render frame;
// the module's demo path resolves on the CPU after reading the buffer back.
#version 450

layout(location = 0) out vec2 uv;

void main() {
    vec2 verts[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 pos = verts[gl_VertexIndex];
    uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
