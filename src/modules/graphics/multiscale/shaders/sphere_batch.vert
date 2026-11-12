#version 450
layout(location=0) in vec3 position;
layout(set=1,binding=0) uniform sampler2D sphereRecords;
layout(set=1,binding=32,std140) uniform RecordLayout { uvec4 dimensions; } recordLayout;
layout(set=1,binding=33,std430) readonly buffer Instances { mat4 transforms[]; } instances;
layout(location=0) out vec2 ndc;
layout(location=1) flat out vec4 centerRadius;
layout(location=2) flat out vec4 viewScale;
layout(location=3) flat out vec3 linearColor;
vec4 record(int index) {
    int width = int(recordLayout.dimensions.x);
    return texelFetch(sphereRecords,ivec2(index%width,index/width),0);
}
void main() {
    int first = gl_InstanceIndex*4;
    centerRadius = record(first);
    viewScale = record(first+1);
    linearColor = record(first+2).rgb;
    vec4 rectangle = record(first+3);
    ndc = mix(rectangle.xy,rectangle.zw,position.xy*0.5+0.5);
    // Sphere packets already encode their view-relative placement. Identity instance
    // transforms preserve the existing engine matrix-buffer ABI without repurposing it.
    gl_Position = instances.transforms[gl_InstanceIndex]*vec4(ndc,0.0,1.0);
}
