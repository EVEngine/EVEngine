#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(location=0) out vec4 outColor;
layout(set=0,binding=1) uniform sampler2D cloudNoise;
layout(push_constant) uniform Params { float p[32]; } u;
// phase, speed (integrated by script), spiral pitch, density, eye radius,
// detail scale, camera elevation, march steps, wall flare. Scene-unit lengths.
float hash(vec3 p) {
    p=fract(p*.3183099+vec3(.11,.27,.39)); p*=17.;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
float noise(vec3 p) {
    vec3 i=floor(p),f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
      mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
// 64 periodic slices, each surrounded by one wrapped texel. Bilinear XY +
// explicit Z interpolation avoids atlas seams without requiring a 3D binding.
vec3 volumeNoise(vec3 p) {
    vec3 q=fract(p)*64.;
    float z0=floor(q.z),z1=mod(z0+1.,64.);
    vec2 tile0=vec2(mod(z0,8.),floor(z0/8.));
    vec2 tile1=vec2(mod(z1,8.),floor(z1/8.));
    vec2 local=(q.xy+1.5)/528.;
    return mix(textureLod(cloudNoise,tile0*(66./528.)+local,0.).rgb,
               textureLod(cloudNoise,tile1*(66./528.)+local,0.).rgb,fract(q.z));
}
float density(vec3 p) {
    vec2 q=p.xz-vec2(3.,-7.);
    float r=length(q),a=atan(q.y,q.x);
    float rot=u.p[2]*.18*log(r+1.)-u.p[0];
    vec2 flow=mat2(cos(rot),-sin(rot),sin(rot),cos(rot))*q;
    vec3 n=volumeNoise(vec3(flow.x,p.y,flow.y)*u.p[5]*.14);
    vec3 fine=volumeNoise(vec3(flow.x,p.y,flow.y)*.27+17.3);
    float spiral=.5+.5*sin(a*3.+log(r+1.)*7.5-u.p[0]*2.+(n.r-.5)*1.2);
    float base=6.0+4.0*exp(-r*.22)+spiral*2.8;
    // Cellular cores push the lower surface outward into round cloud lobes.
    float lower=p.y-base+(n.g-.4)*1.5+(n.r-.5)*1.8+(fine.g-.4)*.5;
    float upper=base+2.5-p.y+(n.r-.5)*1.2;
    float shape=smoothstep(-.16,.30,min(lower,upper));
    float throat=u.p[4]+max(0.,13.-p.y)*u.p[8]+(n.g-.4)*1.1+(fine.g-.4)*.32;
    float eye=smoothstep(throat,throat+.7+(n.r-.5)*.3,r);
    return shape*eye*u.p[3];
}
vec3 sky(vec3 rd) {
    vec3 sun=normalize(vec3(3.,11.,-15.));
    float glow=pow(max(0.,dot(rd,sun)),12.);
    vec3 c=mix(vec3(.19,.27,.34),vec3(.48,.56,.61),max(0.,1.-rd.y));
    c+=vec3(1.,.82,.60)*glow*.25;
    c+=vec3(.30,.23,.14)*pow(max(0.,dot(rd,sun)),70.);
    vec3 haze=mix(vec3(.38,.43,.46),vec3(.62,.52,.38),pow(max(0.,dot(rd,sun)),4.));
    c=mix(c,haze,exp(-max(rd.y,0.)*9.));
    // Layered distant ridges remain below the overhead cloud deck.
    for(int i=0;i<4;i++) {
        float k=float(i),a=atan(rd.x,-rd.z);
        float ridge=-.015-k*.026 + .035*noise(vec3(a*(28.+k*7.),k*9.,0.))
                    +.012*noise(vec3(a*110.,k*3.,1.))+.006*noise(vec3(a*280.,k,2.))
                    +.012*sin(a*(22.+k*4.)+k*3.);
        if(rd.y<ridge) c=mix(vec3(.34,.37,.38),vec3(.055,.075,.088),k/3.);
    }
    return c;
}
void main() {
    // Ray direction follows the actual native camera and sky mesh position.
    vec3 ro=vCameraPos,rd=normalize(vWorldPos-ro);
    vec3 bg=sky(rd),acc=vec3(0.);
    float trans=1.;
    if(rd.y>.025) {
        float start=max(0.,(5.0-ro.y)/rd.y),finish=min(100.,(13.0-ro.y)/rd.y);
        int steps=int(u.p[7]);
        float ds=max(0.,finish-start)/float(steps);
        float jitter=mix(.5,hash(vec3(gl_FragCoord.xy,0.)),.65);
        for(int i=0;i<256;i++) {
            if(i>=steps || trans<.008) break;
            float a0=float(i)/float(steps),a1=float(i+1)/float(steps);
            float t0=mix(start,finish,a0*a0),t1=mix(start,finish,a1*a1);
            ds=t1-t0;
            vec3 p=ro+rd*mix(t0,t1,jitter);
            float d=density(p);
            if(d<.005) continue;
            vec3 ld=normalize(vec3(3.,15.,-7.)-p);
            float occlusion=density(p+ld*.25)*.3+density(p+ld*.75)*.7+density(p+ld*1.8)*1.4;
            float lit=exp(-occlusion*.75);
            float r=length(p.xz-vec2(3.,-7.));
            float inner=exp(-r*.09);
            vec3 ambient=vec3(.12,.17,.22)*(0.65+.35*smoothstep(5.,11.,p.y));
            float micro=volumeNoise(p*.31).b;
            ambient*=.65+micro*.6;
            vec3 direct=vec3(1.0,.84,.62)*lit*(.5+inner*.5)*(.65+micro*.7);
            float alpha=1.-exp(-d*ds*1.3);
            acc+=trans*alpha*(ambient+direct); trans*=1.-alpha;
        }
    }
    vec3 color=acc+trans*bg;
    color=mix(bg,color,smoothstep(.03,.18,rd.y));
    color=pow(max(color,vec3(0)),vec3(.88));
    // Invert the native mesh display curve: authored display palette -> HDR input.
    color=clamp(color,vec3(0.),vec3(.96));
    outColor=vec4(.139*color/max(vec3(.001),1.-color),1.);
}
