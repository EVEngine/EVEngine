struct Varying {@builtin(position) pos:vec4f,@location(0) uv:vec2f,@location(1) coverage:f32};
@fragment fn fs_main(input:Varying)->@location(0) vec4f {
 let alpha=(1.0-smoothstep(0.2,1.0,length(input.uv)))*input.coverage*0.7;
 return vec4f(0.85,0.94,1.0,alpha);
}
