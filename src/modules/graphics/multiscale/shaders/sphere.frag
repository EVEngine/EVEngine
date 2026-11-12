#version 450
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 color;
layout(push_constant) uniform Parameters { vec4 p[8]; } parameters;
void main() {
    vec3 center = parameters.p[0].xyz;
    float radius = parameters.p[0].w;
    vec3 ray = normalize(vec3(ndc.x * parameters.p[1].x, -ndc.y * parameters.p[1].y, 1.0));
    float along = dot(center, ray);
    float perpendicular = length(cross(center, ray));
    float discriminant = (radius - perpendicular) * (radius + perpendicular);
    if (discriminant < 0.0) discard;
    float root = sqrt(discriminant);
    float distance = along - root;
    if (distance <= 0.0) distance = along + root;
    if (distance <= 0.0) discard;
    // Same monotonic physical ray-distance encoding for every scale in this view.
    gl_FragDepth = (log2(distance) + parameters.p[1].z + 2048.0) / 4096.0;
    color = vec4(parameters.p[2].rgb, 1.0);
}
