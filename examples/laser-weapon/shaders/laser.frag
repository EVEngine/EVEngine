#version 450
layout(location=0) in vec4 fragColor;
layout(location=1) in vec2 fragUV;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Params { float p[32]; } u;
float box(vec2 p,vec2 b) { vec2 q=abs(p)-b; return length(max(q,0.))+min(max(q.x,q.y),0.); }
float line(vec2 p,vec2 a,vec2 b) { vec2 v=b-a; return length(p-a-v*clamp(dot(p-a,v)/dot(v,v),0.,1.)); }
float hash(float x) { return fract(sin(x*127.1)*43758.5453); }
float ink(float d) { return 1.-smoothstep(-.001,.0015,d); }
void main() {
    vec2 p=gl_FragCoord.xy/720.; float t=u.p[0];
    vec2 muzzle=vec2(.51,.51), hit=vec2(1.48,.51);
    float charge=smoothstep(0.,.9,t)*(1.-step(.9,t));
    float fire=smoothstep(.9,.94,t)*(1.-smoothstep(2.05,2.2,t));
    float tail=exp(-max(t-2.2,0.)*5.)*step(2.2,t);
    vec3 cyan=vec3(.05,.65,1.), amber=vec3(1.,.25,.035);
    vec3 c=vec3(.008,.016,.029)+vec3(.012,.025,.035)*exp(-length(p-vec2(.9,.5))*2.);
    // Recessed test chamber and floor markings.
    float grid=min(abs(fract(p.x*16.)-.5),abs(fract(p.y*16.)-.5));
    c+=vec3(.022,.045,.057)*(1.-smoothstep(.006,.018,grid));
    c+=vec3(.035,.055,.07)*ink(box(p-vec2(.88,.79),vec2(.8,.004)));
    for(int i=0;i<15;i++) c+=vec3(.025,.05,.07)*ink(box(p-vec2(.15+float(i)*.11,.81),vec2(.024,.003)));
    // Armoured weapon silhouette, barrel shroud, cooling fins and power cell.
    float body=box(p-vec2(.30,.51),vec2(.17,.066));
    c=mix(c,vec3(.075,.105,.14)+.04*(.58-p.y),ink(body));
    c+=vec3(.17,.23,.28)*ink(abs(body)-.002);
    c=mix(c,vec3(.028,.044,.06),ink(box(p-vec2(.26,.64),vec2(.048,.07))));
    c=mix(c,vec3(.11,.16,.20),ink(box(p-vec2(.46,.51),vec2(.065,.038))));
    c+=vec3(.16,.27,.32)*ink(abs(box(p-vec2(.46,.51),vec2(.065,.038)))-.003);
    for(int i=0;i<7;i++) c+=vec3(.1,.18,.23)*ink(box(p-vec2(.19+float(i)*.027,.49),vec2(.005,.033)));
    c+=cyan*(.3+charge)*ink(box(p-vec2(.32,.557),vec2(.092,.004)));
    c+=cyan*.7*ink(box(p-vec2(.48,.51),vec2(.005,.046)));
    // Target slab and concentric calibration rings.
    float target=box(p-vec2(1.51,.51),vec2(.035,.19));
    c=mix(c,vec3(.075,.11,.14),ink(target));
    c+=vec3(.18,.26,.3)*ink(abs(target)-.002);
    vec2 q=p-hit; float r=length(q);
    for(int i=1;i<4;i++) c+=vec3(.05,.11,.14)*ink(abs(r-float(i)*.065)-.001);
    // Charge contracts toward the muzzle. Ring disappears on discharge.
    float mr=length(p-muzzle);
    float ring=.11*(1.-charge)+.022;
    c+=cyan*charge*.003/(abs(mr-ring)+.003);
    c+=cyan*(charge+fire)*.00065/(mr*mr+.0002);
    float beam=line(p,muzzle,hit), flicker=.92+.08*sin(t*93.);
    c+=cyan*fire*flicker*(.22*exp(-beam*25.)+1.5*exp(-beam*130.));
    c+=vec3(.8,.97,1.)*fire*2.4*exp(-beam*650.);
    if(p.x>muzzle.x && p.x<hit.x) {
        float wave=.012*sin(p.x*72.-t*33.)*sin((p.x-muzzle.x)*3.14159/.97);
        c+=cyan*fire*.6*exp(-abs(p.y-.51-wave)*650.);
        c+=cyan*fire*.13*exp(-abs(p.y-.75)*32.);
    }
    // Impact plasma, anisotropic flare and seeded ballistic spark streaks.
    float power=fire+tail*.4;
    c+=amber*power*.0015/(r*r+.0003);
    c+=vec3(.7,.92,1.)*fire*.0005/(r*r+.00008);
    c+=cyan*fire*.5*exp(-abs(q.x)*500.-abs(q.y)*9.);
    float shock=fract(max(t-.9,0.)*2.5);
    c+=amber*fire*(1.-shock)*.17*exp(-abs(r-shock*.24)*320.);
    for(int i=0;i<36;i++) {
        float id=float(i), life=fract(t*1.2+hash(id)), angle=hash(id+42.)*6.28318;
        vec2 v=vec2(cos(angle),sin(angle))*mix(.09,.4,hash(id+8.));
        vec2 a=hit+v*life+vec2(0.,life*life*.16);
        vec2 b=a-v*.045;
        float d=line(p,a,b);
        c+=mix(amber,vec3(1.,.8,.3),hash(id+3.))*power*(1.-life)*exp(-d*800.)*1.8;
    }
    c*=1.-.28*length(p-vec2(.89,.5));
    c=vec3(1.)-exp(-c*1.35);
    outColor=vec4(pow(c,vec3(.85)),1.);
}
