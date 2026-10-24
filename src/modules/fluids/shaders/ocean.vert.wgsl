struct Light3D{posRadius:vec4f,color:vec4f};
struct Frame{mvp:mat4x4f,model:mat4x4f,lightDir:vec4f,lightColor:vec4f,tint:vec4f,cameraPos:vec4f,ambient:vec4f,lights:array<Light3D,8>,texBomb:vec4f,parallax:vec4f,surface:vec4f,view:mat4x4f,clipInfo:vec4f,cloud:vec4f,cloudWind:vec4f,envProbeCenter:vec4f,envProbeExtent:vec4f,skinInfo:vec4f,reflectionProbeCenter:array<vec4f,2>,reflectionProbeExtent:array<vec4f,2>};
@group(0) @binding(0) var<uniform> frame:Frame;
struct Varying {
 @builtin(position) pos:vec4f,
 @location(0) normal:vec3f,
 @location(1) surface:vec2f,
 @location(2) world:vec3f,
};
struct Vertex {@location(0) pos:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f};
@vertex fn vs_main(input:Vertex)->Varying {
 var out:Varying;
 out.pos=frame.mvp*vec4f(input.pos,1.0);out.pos.y=-out.pos.y;
 out.world=(frame.model*vec4f(input.pos,1.0)).xyz;
 out.normal=normalize(mat3x3f(frame.model[0].xyz,frame.model[1].xyz,frame.model[2].xyz)*input.normal);
 out.surface=input.uv;return out;
}
