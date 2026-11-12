#version 450
layout(location=0) in vec2 ndc;
layout(location=1) flat in vec4 centerRadius;
layout(location=2) flat in vec4 viewScale;
layout(location=3) flat in vec3 linearColor;
layout(location=0) out vec4 color;
void main() {
    vec3 center = centerRadius.xyz;
    float radius = centerRadius.w;
    vec3 ray = normalize(vec3(ndc.x*viewScale.x,-ndc.y*viewScale.y,1.0));
    float along = dot(center,ray);
    float perpendicular = length(cross(center,ray));
    float discriminant = (radius-perpendicular)*(radius+perpendicular);
    if (discriminant<0.0) discard;
    float root = sqrt(discriminant);
    float distance = along-root;
    if (distance<=0.0) distance=along+root;
    if (distance<=0.0) discard;
    gl_FragDepth = (log2(distance)+viewScale.z+2048.0)/4096.0;
    color = vec4(linearColor,1.0);
}
