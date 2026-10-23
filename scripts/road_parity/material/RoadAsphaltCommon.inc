// Shared GLSL/HLSL road material. Coordinates are canonical EVEngine RH Y-up metres.
// Native textures and Unity comparison imports are raw UNORM; decode albedo once.
float roadDecode(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 roadAlbedo(float3 c) { return float3(roadDecode(c.x),roadDecode(c.y),roadDecode(c.z)); }
float2 roadMirror(float2 q) { return 1.0 - abs(frac(q * 0.5) * 2.0 - 1.0); }
float2 roadUV(float2 q) { return 0.05 + 0.9 * roadMirror(q / ROAD_PERIOD); }
float3 roadBase(float2 q) { return roadAlbedo(ROAD_SAMPLE(roadColor,roadUV(q)).rgb); }
float3 roadGrain(float2 q) {
    float2 m = roadMirror(q / ROAD_PERIOD);
    float2 g = ROAD_SAMPLE(roadNormal,0.05 + 0.9 * m).rg * 2.0 - 1.0;
    // Taper perturbation at mirror boundaries so normal detail also has continuous seams.
    g *= sin(3.14159265359 * m.x) * sin(3.14159265359 * m.y);
    return float3(g.x,g.y,0.0);
}
float3 roadShade(float3 p, float3 normal, float3 eye) {
    float3 n = normalize(normal);
    float3 w = n*n; w *= w; w /= max(w.x+w.y+w.z,0.00001);
    float3 albedo = roadBase(p.yz)*w.x + roadBase(p.xz)*w.y + roadBase(p.xy)*w.z;
    float3 gx=roadGrain(p.yz),gy=roadGrain(p.xz),gz=roadGrain(p.xy);
    float3 perturb=float3(0.0,gx.x,gx.y)*w.x + float3(gy.x,0.0,gy.y)*w.y + float3(gz.x,gz.y,0.0)*w.z;
    n=normalize(n + ROAD_NORMAL_STRENGTH*(perturb-n*dot(n,perturb)));
    float3 l=normalize(ROAD_LIGHT_DIRECTION);
    float3 v=normalize(eye-p),h=normalize(l+v);
    float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.0001),nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float roughness=ROAD_ROUGHNESS, a=roughness*roughness, a2=a*a;
    float d=nh*nh*(a2-1.0)+1.0;
    float D=a2/max(3.14159265359*d*d,0.0001);
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    float G=(nl/(nl*(1.0-k)+k))*(nv/(nv*(1.0-k)+k));
    float F=0.04+0.96*pow(1.0-vh,5.0);
    float specular=D*G*F/max(4.0*nl*nv,0.0001);
    return albedo*ROAD_AMBIENT + (albedo*(1.0-F)/3.14159265359+float3(specular,specular,specular))*nl;
}
