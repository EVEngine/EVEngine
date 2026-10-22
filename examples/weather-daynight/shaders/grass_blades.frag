#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 mvp; mat4 model; vec4 lightDirIntensity; vec4 lightColor;
    vec4 tint; vec4 cameraPos; vec4 ambient;
} frame;
layout(location=0) out vec4 outColor;
void main() {
    vec3 N=normalize(vNormal)*(gl_FrontFacing ? 1.0 : -1.0);
    vec3 L=normalize(frame.lightDirIntensity.xyz);
    float diffuse=0.22+0.55*max(dot(N,L),0.0)+0.13*max(dot(-N,L),0.0);
    // Per-blade palette variation, with dark roots and subdued olive tips.
    vec3 root=mix(vec3(0.021,0.045,0.018),vec3(0.045,0.065,0.025),vUV.x);
    vec3 tip=mix(vec3(0.075,0.135,0.042),vec3(0.14,0.16,0.065),vUV.x);
    vec3 albedo=mix(root,tip,smoothstep(0.0,1.0,vUV.y));
    vec3 lighting=frame.ambient.rgb*1.35+frame.lightColor.rgb*diffuse;
    outColor=vec4(albedo*lighting*vTint.rgb,1.0);
}
