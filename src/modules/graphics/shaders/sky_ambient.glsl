// Per-vertex preparation for the three-vertex sky draw, never per fragment.
// The CPU bakeDistantSkyAmbient routine supplies an independent numerical oracle.
float skyFraction(inout uint seed) {
    seed = seed * 196314165u + 907633515u;
    return float(seed >> 9) / 8388608.0;
}
vec3 distantSkyAmbient(vec3 light) {
    vec3 origin = vec3(0,atmosphere.geometry.x+6,0);
    vec3 sum = vec3(0);
    uint seed = 0xde4dc0deu;
    for (int i=0; i<8; ++i) {
        for (int j=0; j<8; ++j) {
            float u = (float(i)+skyFraction(seed))/8;
            float v = (float(j)+skyFraction(seed))/8;
            float y = 1-2*u;
            float radius = sqrt(max(0,1-y*y));
            vec3 direction = vec3(radius*cos(2*PI*v),y,radius*sin(2*PI*v));
            float end = sphere(origin,direction,atmosphere.geometry.y).y;
            vec2 ground = sphere(origin,direction,atmosphere.geometry.x);
            if (ground.x>0) end=min(end,ground.x);
            float stepLength=end/10;
            vec3 throughput=vec3(1), radiance=vec3(0);
            for (int k=0;k<10;++k) {
                vec3 point=origin+direction*((float(k)+.3)*stepLength);
                vec3 extinction, rayleigh, mie;
                medium(point,extinction,rayleigh,mie);
                vec3 segment=exp(-extinction*stepLength);
                vec3 source=(rayleigh+mie)*(sunlight(point,light)/(4*PI)+multipleScattering(point,light));
                radiance+=throughput*source*(1-segment)/max(extinction,vec3(1e-8));
                throughput*=segment;
            }
            sum+=radiance;
        }
    }
    return sum/64;
}
