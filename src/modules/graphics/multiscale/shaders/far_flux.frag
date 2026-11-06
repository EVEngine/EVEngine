#version 450
layout(set=1,binding=0) uniform sampler2D fluxImage;
layout(push_constant) uniform Exposure { float p[32]; } exposure;
layout(location=0) out vec4 color;
void main() {
    vec3 flux = texelFetch(fluxImage, ivec2(gl_FragCoord.xy), 0).rgb;
    color = vec4(1.0 - exp(-exposure.p[0] * flux), 1.0);
    gl_FragDepth = 0.99999994;
}
