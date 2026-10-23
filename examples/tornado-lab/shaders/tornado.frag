#version 450
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Params { float p[32]; } u;
// 0 time, 1 height, 2 radius, 3 density, 4 twist, 5 turbulence,
// 6 dust, 7 bend, 8 orbit, 9 steps, 10..17 native spline samples.
float hash(vec3 p) { p=fract(p*.3183099+vec3(.11,.27,.39)); p*=17.; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }
float noise(vec3 p) {
    vec3 i=floor(p), f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float fbm(vec3 p) { return .57*noise(p)+.28*noise(p*2.03+5.1)+.15*noise(p*4.07+11.7); }
float profile(float h) {
    float f=clamp(h*7.,0.,6.9999); int k=min(int(f),6);
    return max(.04,mix(u.p[10+k],u.p[11+k],fract(f)));
}
vec2 medium(vec3 p) {
    float h=p.y/u.p[1], t=u.p[0];
    vec2 center=vec2(sin(h*2.8+t*.31),cos(h*3.7-t*.23))*u.p[7]*h*h;
    vec2 q=p.xz-center;
    float r=length(q), a=atan(q.y,q.x);
    float radius=u.p[2]*profile(h);
    vec3 flow=vec3(q*cos(t*.2)+vec2(-q.y,q.x)*sin(t*.2),p.y-t*.7);
    float n=fbm(flow*2.1+vec3(sin(a*3.+h*12.-t*2.))*.3);
    float strands=sin(a*9.+h*u.p[4]*16.-t*5.+n*u.p[5]*5.);
    float shell=exp(-pow((r-radius-(n-.5)*u.p[5]*.5)/max(.14,radius*.42),2.));
    float wisps=noise(flow*8.+vec3(h*12.,t*.6,0.));
    float body=shell*(.32+.68*n)*(.60+.40*strands)*(.65+.6*wisps);
    body*=smoothstep(-.015,.04,h)*(1.-smoothstep(.90,1.10,h));
    float dust=exp(-p.y*p.y/ .30)*exp(-r*r/12.)*u.p[6]*(.25+fbm(vec3(q*1.9,p.y-t)));
    float anvil=exp(-pow((p.y-u.p[1]+(n-.5)*.8)/.65,2.))*exp(-r*r/22.)*(.15+.9*n);
    return vec2(max(0.,body*u.p[3]+dust+anvil*u.p[3]*.5),clamp(dust/(body+anvil+.001),0.,1.));
}
void main() {
    vec2 uv=(gl_FragCoord.xy-vec2(310.,0.))/vec2(970.,800.);
    vec2 xy=(uv*2.-1.)*vec2(970./800.,-1.);
    float angle=u.p[8];
    vec3 ro=vec3(sin(angle)*15.,5.0,cos(angle)*15.);
    vec3 ww=normalize(vec3(0.,u.p[1]*.48,0.)-ro);
    vec3 right=normalize(cross(ww,vec3(0,1,0))), up=cross(right,ww);
    vec3 rd=normalize(ww*1.75+right*xy.x+up*xy.y);
    vec3 sky=mix(vec3(.32,.38,.40),vec3(.055,.09,.12),clamp(rd.y*1.7+.45,0.,1.));
    float ground=rd.y<0.?-ro.y/rd.y:100.;
    if(ground<60.) {
        vec3 gp=ro+rd*ground;
        float grit=fbm(gp*.8);
        sky=mix(vec3(.14,.14,.115),vec3(.25,.24,.19),grit);
        sky*=1.-.42*exp(-dot(gp.xz,gp.xz)/18.);
        sky=mix(sky,vec3(.35,.39,.39),1.-exp(-ground*.018));
    }
    // Bounded fixed ray march; stable world-space field, no per-frame allocations.
    int steps=int(u.p[9]); float start=7., finish=min(24.,ground);
    float ds=max(0.,finish-start)/float(steps), trans=1.; vec3 color=vec3(0.);
    float jitter=hash(vec3(gl_FragCoord.xy,0.));
    for(int i=0;i<128;++i) {
        if(i>=steps||trans<.012) break;
        vec3 p=ro+rd*(start+(float(i)+jitter)*ds);
        if(p.y<0.||p.y>u.p[1]+2.) continue;
        vec2 d=medium(p); if(d.x<.012) continue;
        float shadow=medium(p+vec3(-.45,.6,.4)).x;
        float light=exp(-shadow*1.15);
        vec3 cold=mix(vec3(.105,.14,.155),vec3(.68,.74,.75),light);
        vec3 warm=mix(vec3(.19,.15,.10),vec3(.55,.46,.32),light);
        vec3 scattering=mix(cold,warm,d.y*.75);
        float alpha=1.-exp(-d.x*ds*2.6);
        color+=trans*alpha*scattering; trans*=1.-alpha;
    }
    color+=trans*sky;
    color*=1.-.18*dot(uv-.5,uv-.5);
    color=pow(max(color,vec3(0.)),vec3(.90));
    outColor=vec4(color,1.);
}
