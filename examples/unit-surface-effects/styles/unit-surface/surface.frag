#version 450
// Concept catalogue: reference/ART_DIRECTION.md. Local-space surface motifs.
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=3) in vec3 vLightDir;
layout(location=4) in vec3 vLightColor;
layout(location=2) in vec4 vTint;
layout(location=5) in vec3 vWorldPos;
layout(location=6) in vec3 vCameraPos;
layout(location=7) in vec3 vLocalPos;
layout(location=8) in vec3 vLocalNormal;
layout(push_constant) uniform Params { float p[32]; } u;
layout(location=0) out vec4 outColor;
float hash(vec3 p) { return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }
float noise(vec3 p) {
    vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),
                   mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),
                   mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float line(float d,float w) { return 1.0-smoothstep(w,w+max(fwidth(d),0.008),abs(d)); }
float stripe(float x,float w) { return line(fract(x)-0.5,w); }
// Nearest two Voronoi distances: stable polygon fragments, not a recolored noise map.
vec3 cells(vec3 p) {
    vec3 base=floor(p); float first=10.0,second=10.0,id=0.0;
    for(int z=-1;z<=1;++z) for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        vec3 cell=base+vec3(x,y,z);
        vec3 jitter=vec3(hash(cell),hash(cell+17.0),hash(cell+39.0));
        float d=length(cell+0.15+0.7*jitter-p);
        if(d<first) {second=first;first=d;id=hash(cell+71.0);}
        else second=min(second,d);
    }
    return vec3(first,second-first,id);
}
float hexEdges(vec2 p,float width) {
    const vec2 stepSize=vec2(1.0,1.7320508);
    vec2 a=mod(p,stepSize)-stepSize*0.5;
    vec2 b=mod(p-stepSize*0.5,stepSize)-stepSize*0.5;
    vec2 h=dot(a,a)<dot(b,b)?a:b;
    return line(0.5-max(abs(h.x),dot(abs(h),vec2(0.5,0.8660254))),width);
}
vec3 rotateQ(vec4 q,vec3 p) {return p+2.0*cross(q.xyz,cross(q.xyz,p)+q.w*p);}
vec3 unitPosition() {
    if(u.p[31]<0.5) return vLocalPos;
    vec4 inverseQ=vec4(-u.p[18],-u.p[19],-u.p[20],u.p[21]);
    return rotateQ(inverseQ,(vWorldPos-vec3(u.p[15],u.p[16],u.p[17]))/u.p[22]);
}
vec3 unitNormal() {
    if(u.p[31]<0.5) return normalize(vLocalNormal);
    return normalize(rotateQ(vec4(-u.p[18],-u.p[19],-u.p[20],u.p[21]),normalize(vNormal)));
}
float fbm(vec3 p) {
    return noise(p)*0.55+noise(p*2.07+13.0)*0.27+noise(p*4.13+29.0)*0.13+noise(p*8.31)*0.05;
}
float teleportPhase(float t) {return u.p[30]>0.5?u.p[14]:fract(t/max(u.p[28],0.01));}
float teleportField(vec3 p,float t) {
    vec3 cell=floor(p/0.125);float id=hash(cell+u.p[13]);
    return (cell.y+0.5)*0.125-mix(0.25,3.65,teleportPhase(t))+(id-0.5)*0.19;
}
struct Wood {vec3 albedo;float height;float seam;};
Wood wood(vec3 pos,float t) {
    vec3 p=pos+vec3(u.p[13]*0.17,0,0);
    float bend=fbm(p*vec3(3.0,0.8,3.0));
    vec3 fibersPosition=p*vec3(18.0,1.1,18.0)+vec3(bend*1.8,0,bend*0.7);
    vec3 pieces=cells(fibersPosition);
    float crack=1.0-smoothstep(0.025,0.11,pieces.y);
    float broad=fbm(p*vec3(15.0,2.0,15.0));
    float fine=fbm(p*vec3(60.0,5.0,60.0));
    vec2 knot=(p.xy-vec2(0.28,2.02))*vec2(10.0,8.0);
    float knotMask=exp(-dot(knot,knot)*1.4);
    float rings=0.5+0.5*sin(length(knot)*16.0+bend);
    Wood woodValue;
    woodValue.albedo=mix(vec3(0.055,0.035,0.018),vec3(0.29,0.20,0.10),broad);
    woodValue.albedo*=0.7+pieces.z*0.55;
    woodValue.albedo*=0.80+fine*0.45;
    woodValue.albedo*=1.0-crack*0.50;
    woodValue.albedo*=1.0-knotMask*(1.0-rings)*0.35;
    woodValue.seam=crack*step(0.64,pieces.z)*smoothstep(0.3,0.6,broad)*(0.8+0.2*sin(t+p.y));
    woodValue.albedo=mix(woodValue.albedo,vec3(0.055,0.21,0.015),woodValue.seam*0.8);
    woodValue.height=broad*0.0015+fine*0.0003-crack*0.0015+knotMask*rings*0.0004;
    return woodValue;
}
vec3 bumpNormal(vec3 n,float height) {
    vec3 dx=dFdx(vWorldPos),dy=dFdy(vWorldPos);
    vec3 a=cross(dy,n),b=cross(n,dx);float det=dot(dx,a);
    vec3 gradient=(a*dFdx(height)+b*dFdy(height))/max(abs(det),0.000001)*sign(det);
    return normalize(n-gradient);
}
vec3 litMaterial(vec3 albedo,vec3 n,float roughness,float metallic) {
    vec3 l=normalize(vLightDir),v=normalize(vCameraPos-vWorldPos),h=normalize(l+v);
    float diffuse=max(dot(n,l),0.0),facing=max(dot(n,v),0.0);
    float spec=pow(max(dot(n,h),0.0),mix(160.0,6.0,roughness));
    vec3 f0=mix(vec3(0.035),albedo,metallic);
    vec3 fresnel=f0+(1.0-f0)*pow(1.0-facing,5.0);
    vec3 fill=albedo*vec3(0.10,0.14,0.22)*(0.4+0.6*max(n.y,0.0));
    return albedo*(vec3(0.035)+vLightColor*diffuse*(1.0-metallic*0.4))+fill+
        fresnel*spec*(2.0-roughness)*vLightColor;
}
float electricDistance(vec3 p,float tick) {
    float best=10.0;
    for(int i=0;i<3;++i) {
        float id=float(i),base=mix(-0.95,0.95,id/2.0);
        if(hash(vec3(id,tick,19))<0.28) continue;
        float y=p.y*6.5,segment=floor(y),f=fract(y);
        float a=hash(vec3(segment,id,tick))*2.0-1.0,b=hash(vec3(segment+1.0,id,tick))*2.0-1.0;
        float trunk=base+mix(a,b,f)*0.19+sin(p.z*4.0+id)*0.08;
        float gate=smoothstep(1.0,1.20,p.y)*(1.0-smoothstep(2.50,2.65,p.y));
        best=min(best,abs(p.x-trunk)+(1.0-gate)*5.0);
        float root=1.35+hash(vec3(id,tick,23))*0.8;
        float side=hash(vec3(id,tick,37))>0.5?1.0:-1.0;
        float branch=base+side*(p.y-root)*0.9+mix(a,b,f)*0.1;
        float branchGate=step(root,p.y)*step(p.y,root+0.65);
        best=min(best,abs(p.x-branch)+(1.0-branchGate)*5.0);
    }
    return best;
}
float segmentDistance(vec2 p,vec2 a,vec2 b) {
    vec2 d=b-a;float along=clamp(dot(p-a,d)/dot(d,d),0.0,1.0);
    return length(p-a-d*along);
}
float frostDendrite(vec2 p) {
    vec2 cell=floor(p*4.2),q=fract(p*4.2)-0.5;
    float id=hash(vec3(cell,u.p[13]));
    float angle=id*6.283185;
    q=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*q;
    q*=0.8+id*0.9;
    q.x+=(noise(vec3(q*5.0,id))*2.0-1.0)*0.09;
    float d=segmentDistance(q,vec2(0,-0.34),vec2(0,0.35));
    for(int k=0;k<3;++k) {
        float y=-0.2+float(k)*0.16;
        float span=(0.22-float(k)*0.035)*(0.7+hash(vec3(cell,float(k)))*0.6);
        d=min(d,segmentDistance(q,vec2(0,y),vec2(span,y+0.18)));
        d=min(d,segmentDistance(q,vec2(0,y),vec2(-span,y+0.18)));
    }
    return line(d,0.008)*smoothstep(0.2,0.55,id);
}
float frostPattern(vec3 p) {
    vec3 weights=pow(abs(unitNormal()),vec3(6.0));weights/=weights.x+weights.y+weights.z;
    float branches=dot(weights,vec3(frostDendrite(p.zy*2.1),frostDendrite(p.xz*2.1),frostDendrite(p.xy*2.1)));
    float facets=line(cells(p*14.0+fbm(p*2.0)*0.2).y,0.008);
    return facets*0.65+branches*0.12;
}
vec2 waterDrops(vec3 p,float t) {
    vec2 uv=abs(unitNormal().x)>0.7?p.zy:p.xy;
    vec2 grid=vec2(uv.x*11.0,uv.y*4.0+t*1.6);
    vec2 cell=floor(grid),q=fract(grid)-0.5;
    float id=hash(vec3(cell,u.p[13]));
    q.x+=(id-0.5)*0.55;
    float bead=exp(-dot(q/vec2(0.055,0.12),q/vec2(0.055,0.12)));
    float trail=exp(-abs(q.x)*90.0)*smoothstep(0.0,0.12,q.y)*(1.0-smoothstep(0.2,0.5,q.y));
    return vec2(bead,trail)*step(0.62,id);
}
float runeDistance(vec2 p,float kind) {
    float d=segmentDistance(p,vec2(-0.18,-0.3),vec2(-0.18,0.3));
    if(kind<0.25) {
        d=min(d,segmentDistance(p,vec2(-0.18,0.25),vec2(0.2,0.05)));
        d=min(d,segmentDistance(p,vec2(0.2,0.05),vec2(-0.12,-0.15)));
    } else if(kind<0.5) {
        d=min(d,abs(abs(p.x+0.02)+abs(p.y)-0.28)*0.7);
    } else if(kind<0.75) {
        d=min(d,segmentDistance(p,vec2(-0.18,0.18),vec2(0.2,0.18)));
        d=min(d,segmentDistance(p,vec2(0.2,0.18),vec2(-0.18,-0.2)));
    } else {
        d=min(d,segmentDistance(p,vec2(-0.18,0.0),vec2(0.2,0.25)));
        d=min(d,segmentDistance(p,vec2(0.2,0.25),vec2(0.2,-0.25)));
    }
    return d;
}
float circuitDistance(vec2 p) {
    float d=10.0;
    for(int side=-1;side<=1;side+=2) {
        float s=float(side);
        d=min(d,segmentDistance(p,vec2(s*1.04,-0.95),vec2(s*1.04,-0.30)));
        d=min(d,segmentDistance(p,vec2(s*1.04,-0.30),vec2(s*0.54,0.20)));
        d=min(d,segmentDistance(p,vec2(s*0.54,0.20),vec2(s*0.22,0.20)));
        d=min(d,segmentDistance(p,vec2(s*0.22,0.20),vec2(0,0)));
    }
    for(int row=0;row<5;++row) {
        float y=-0.18-float(row)*0.20;
        float span=0.47-float(row)*0.045;
        for(int side=-1;side<=1;side+=2) {
            float s=float(side);
            d=min(d,segmentDistance(p,vec2(s*0.09,-0.08),vec2(s*0.16,y+0.08)));
            d=min(d,segmentDistance(p,vec2(s*0.16,y+0.08),vec2(s*span,y)));
            d=min(d,segmentDistance(p,vec2(s*span,y),vec2(s*(span+0.06),y+0.09)));
        }
    }
    return d;
}
void main() {
    float t=u.p[0]*u.p[1], density=u.p[7], w=u.p[11];
    float warp=u.p[12], seed=u.p[13], progress=u.p[14];
    vec3 unitPos=unitPosition();
    vec3 q=unitPos*density;
    vec3 p=q+seed*vec3(0.37,0.73,0.19);
    float rim=pow(1.0-clamp(abs(dot(normalize(vNormal),normalize(vCameraPos-vWorldPos))),0.0,1.0),u.p[8]);
    float n=noise(p+vec3(0,-t,0));
    float wave=0.5+0.5*sin(q.y*6.283185-t*3.0);
    float coverage=u.p[9], mask=0.0;
    vec3 color=vec3(u.p[2],u.p[3],u.p[4]);
    int mode=int(u.p[10]+0.5);
    if(u.p[27]>3.5) {
        if(mode==14) {
            vec3 flow=vLocalPos*vec3(4.5,2.5,4.5)+vec3(sin(t*0.7)*0.3,-t*0.5,0);
            float cloud=fbm(flow+fbm(flow*1.8)*1.5);
            float edge=line(cloud-0.50,0.018);
            float widthFade=1.0-smoothstep(0.2,1.0,abs(vUV.x));
            float ends=sin(clamp(vUV.y,0.0,1.0)*3.141593);
            float opacity=smoothstep(0.25,0.65,cloud)*widthFade*ends*0.65*u.p[6]*u.p[5];
            outColor=vec4(mix(vec3(0.025,0.002,0.045),vec3(0.20,0.012,0.35),edge)*u.p[29],opacity);
        } else if(mode==5 || mode==24) {
            float pulse=0.7+0.3*sin(t*1.5+vLocalPos.z);
            vec3 tint=mode==5?vec3(0.06,1.0,0.48):vec3(0.04,0.30,1.0);
            outColor=vec4(tint*u.p[29]*1.4,0.65*pulse*u.p[6]*u.p[5]);
        } else if(mode==2) {
            float gleam=0.65+0.35*sin(t*2.0+vLocalPos.y*9.0);
            outColor=vec4(vec3(0.20,0.65,1.0)*u.p[29],0.26*gleam*u.p[6]*u.p[5]);
        } else {
            outColor=vec4(vec3(0.32,0.6,0.85)*u.p[29],0.23*u.p[6]*u.p[5]);
        }
        return;
    }
    if(u.p[27]>2.5) {
        float age=mod(t,max(u.p[28],0.01));
        float ring=exp(-age*7.0)*u.p[6];
        outColor=vec4(vec3(u.p[2],u.p[3],u.p[4])*u.p[29],ring);return;
    }
    if(u.p[27]>1.5) {
        float field=teleportField(vLocalPos,t);
        float visibility=(1.0-smoothstep(0.08,0.38,abs(field)))*u.p[6];
        vec3 fragmentColor=mix(color,vec3(0.32,0.04,1.0),step(0.55,hash(floor(vLocalPos/0.125)+vec3(31.0,7.0,13.0))));
        outColor=vec4(fragmentColor*u.p[29]*0.6,visibility);return;
    }
    float phaseBand=fract(unitPos.y*2.8-t*0.6+hash(vec3(floor(unitPos.y*2.8),seed,4))*0.3);
    if(mode==9 && phaseBand<0.25) discard;
    if(u.p[27]>0.5) {
        if(mode==23 && teleportField(unitPos,t)<0.0) discard;
        vec3 normalValue=normalize(vNormal),albedo=vec3(0.16,0.19,0.24);
        float roughness=0.32,metallic=0.72,baseAlpha=1.0;
        if(vUV.x>0.5) {albedo=vec3(0.015,0.02,0.029);roughness=0.68;metallic=0.1;}
        if(vUV.x>1.5) {albedo=vec3(0.008,0.025,0.04);roughness=0.13;metallic=0.6;}
        vec3 materialGlow=vec3(0);
        if(mode==2) {
            float plateEdge=step(0.0001,vUV.y)*(1.0-smoothstep(0.02,0.12,vUV.y));
            float coat=smoothstep(0.30,0.62,fbm(unitPos*5.0)+unitNormal().y*0.25+plateEdge*0.25)*coverage;
            vec3 crystalCell=cells(unitPos*14.0+fbm(unitPos*2.0)*0.2);
            float crystal=frostPattern(unitPos);
            vec3 ice=vec3(0.035,0.12,0.22)*(0.65+crystalCell.z*0.8);
            albedo=mix(albedo,ice,coat);
            albedo+=vec3(0.06,0.13,0.19)*crystal*coat;
            roughness=mix(0.14,0.34,coat);metallic=0.18;
            normalValue=bumpNormal(normalValue,(fbm(unitPos*38.0)*0.001+crystal*0.0004)*coat);
        } else if(mode==3) {
            float scorch=smoothstep(0.3,0.6,fbm(unitPos*3.0));
            albedo*=1.0-scorch*0.92;roughness=0.9;metallic=0.0;
            normalValue=bumpNormal(normalValue,fbm(unitPos*30.0)*0.006);
        } else if(mode==4) {
            float corrosion=smoothstep(0.45,0.7,fbm(unitPos*5.0));
            albedo=mix(albedo,vec3(0.012,0.04,0.005),corrosion);roughness=0.3;
            normalValue=bumpNormal(normalValue,-corrosion*0.007);
        } else if(mode==7) {
            vec3 c=cells(unitPos*4.0+fbm(unitPos*3.0)*0.3);
            float cracks=1.0-smoothstep(0.015,0.06,c.y);
            float growth=u.p[30]>0.5?progress:min(t/max(u.p[28],0.01),1.0);
            float stone=step(c.z,coverage*growth);
            albedo=mix(albedo,vec3(0.14,0.135,0.12)*(0.65+c.z*0.8)*(1.0-cracks*0.85),stone);
            normalValue=bumpNormal(normalValue,(fbm(unitPos*40.0)*0.003-cracks*0.008)*stone);
            roughness=0.95;metallic=0.0;
        } else if(mode==8) {
            baseAlpha=(0.04+rim*0.10)*u.p[6];albedo*=0.5;
        } else if(mode==16) {
            albedo*=0.22;roughness=0.7;metallic=0.4;
        } else if(mode==14) {
            float cloud=fbm(unitPos*3.0+vec3(0,-t*0.15,0));
            albedo=mix(albedo,vec3(0.028,0.009,0.047),0.40+cloud*0.40);
            roughness=0.6;metallic=0.3;
        } else if(mode==17) {
            vec2 water=waterDrops(unitPos,t);
            albedo*=0.45;roughness=0.10;metallic=0.05;
            normalValue=bumpNormal(normalValue,water.x*0.0012+water.y*0.0003);
            vec3 reflected=reflect(-normalize(vCameraPos-vWorldPos),normalValue);
            float wetGlint=pow(max(dot(reflected,normalize(vec3(-0.5,0.8,0.9))),0.0),45.0);
            materialGlow=vec3(0.25,0.35,0.42)*wetGlint;
        } else if(mode==18) {
            float grains=hash(floor(unitPos*170.0));
            float sand=smoothstep(0.25,0.6,fbm(unitPos*4.0));
            albedo=mix(albedo,vec3(0.29,0.15,0.045)*(0.65+grains*0.55),sand);
            normalValue=bumpNormal(normalValue,noise(unitPos*100.0)*0.0015);
            roughness=0.98;metallic=0.0;
        } else if(mode==19) {
            // Smooth hammered variation avoids discontinuous per-cell normal tiles.
            vec3 offset=vec3(noise(unitPos*8.0),noise(unitPos*8.0+17),noise(unitPos*8.0+39))-0.5;
            if(u.p[31]>0.5) offset=rotateQ(vec4(u.p[18],u.p[19],u.p[20],u.p[21]),offset);
            normalValue=normalize(normalValue+offset*0.045);
            albedo=vec3(0.018,0.024,0.033);roughness=0.12;metallic=1.0;
            vec3 reflected=reflect(-normalize(vCameraPos-vWorldPos),normalValue);
            float upper=smoothstep(-0.15,0.55,reflected.y);
            float boxLight=pow(max(dot(reflected,normalize(vec3(-0.65,0.8,0.9))),0.0),32.0);
            float stripLight=exp(-pow((reflected.x-0.38)/0.14,2.0))*smoothstep(-0.5,0.5,reflected.y);
            materialGlow=vec3(0.008,0.014,0.025)+vec3(0.085,0.090,0.10)*upper+
                vec3(1.8,1.95,2.1)*boxLight+vec3(0.95,0.98,1.0)*stripLight+
                vec3(0.20,0.19,0.18)*exp(-pow((reflected.y+0.25)/0.08,2.0));
        } else if(mode==20) {
            Wood woodValue=wood(unitPos,t);albedo=woodValue.albedo;
            normalValue=bumpNormal(normalValue,woodValue.height);roughness=0.88;metallic=0.0;
        } else if(mode==21) {
            vec3 c=cells(unitPos*3.0);float crack=1.0-smoothstep(0.01,0.05,c.y);
            albedo*=0.22+0.35*c.z;roughness=0.5;
            normalValue=bumpNormal(normalValue,-crack*0.01);
            if(u.p[30]>0.5 && c.z<progress*0.8) discard;
        }
        outColor=vec4((litMaterial(albedo,normalValue,roughness,metallic)+materialGlow)*vTint.rgb,baseAlpha);return;
    }

    if(mode==0) {
        vec3 weights=pow(abs(unitNormal()),vec3(8));weights/=weights.x+weights.y+weights.z;
        float hex=dot(weights,vec3(hexEdges(p.yz,w),hexEdges(p.xz,w),hexEdges(p.xy,w)));
        float sweep=pow(0.5+0.5*sin(unitPos.y*4.0-t*2.8),10.0);
        float rimPulse=0.65+0.35*sin(t*2.4);
        mask=hex*(0.13+0.45*sweep+rim*0.65)+rim*rimPulse*0.60;
        color*=0.7+0.5*sweep;
    } else if(mode==1) {
        float age=mod(t,max(u.p[28],0.01));
        float d=length(unitPos-vec3(u.p[23],u.p[24],u.p[25]));
        float flash=exp(-d*d/(u.p[26]*u.p[26]))*exp(-age*24.0);
        float waveRadius=0.07+age*4.0;
        float ring=exp(-abs(d-waveRadius)*45.0)*exp(-age*12.0);
        float branches=line(cells(unitPos*7.0).y,0.018)*exp(-d*5.0)*exp(-age*18.0);
        float core=exp(-d*d/0.004)*exp(-age*14.0);
        mask=flash*0.55+ring+branches*0.8+core;
        color=mix(color,vec3(1.0,0.85,0.65),core*0.9);
    } else if(mode==2) {
        float crystal=frostPattern(unitPos);
        float plateEdge=step(0.0001,vUV.y)*(1.0-smoothstep(0.02,0.12,vUV.y));
        float cluster=smoothstep(0.40,0.64,fbm(unitPos*4.0)+unitNormal().y*0.22+plateEdge*0.2);
        float glint=pow(0.5+0.5*sin(unitPos.y*5.0-t*3.0+fbm(unitPos*4.0)*8.0),12.0);
        mask=crystal*cluster*(0.08+0.18*glint)+plateEdge*0.20*cluster+rim*0.18*coverage;
        color=mix(vec3(0.12,0.5,1.0),vec3(0.7,0.92,1.0),glint*0.7);
    } else if(mode==3) {
        vec3 c=cells(unitPos*3.5+vec3(fbm(unitPos*3.0)*0.5,-t*0.08,0));
        float core=line(c.y,0.009),corona=exp(-c.y*38.0);
        float flame=pow(0.5+0.5*sin(unitPos.y*4.0-t*3.0+c.z*8.0),2.0);
        mask=(core*0.7+corona*0.35)*(0.25+0.75*flame)*step(c.z,coverage);
        color=mix(color,vec3(1,0.65,0.035),core*flame*0.55);
    } else if(mode==4) {
        float corrosion=fbm(unitPos*4.0+vec3(sin(t*0.3)*0.12,-t*0.12,0));
        float border=line(corrosion-0.48,0.012);
        mask=border*0.65+smoothstep(0.58,0.75,corrosion)*0.22;
        color*=0.65+0.35*sin(t*2.0+unitPos.y*2.0);
    } else if(mode==5) {
        float height=mod(t*1.25,4.1)-0.3;
        float band=exp(-abs(unitPos.y-height)*20.0);
        vec2 glyph=fract(unitPos.xy*4.0+vec2(0,-t*0.35))-0.5;
        float crossMask=line(min(abs(glyph.x),abs(glyph.y)),0.035)*
            (1.0-smoothstep(0.16,0.22,max(abs(glyph.x),abs(glyph.y))));
        float rare=step(0.94,hash(vec3(floor(unitPos.xy*4.0),seed)));
        mask=band*0.25+crossMask*rare*0.55+rim*0.035;
    } else if(mode==6) {
        float tick=floor(t*11.0),distance=electricDistance(unitPos,tick);
        float core=line(distance,0.007),corona=exp(-distance*50.0);
        float flicker=0.6+0.4*hash(vec3(tick,seed,0));
        mask=(core*0.7+corona*0.35+exp(-distance*16.0)*0.1)*flicker;
        color=mix(color,vec3(0.55,0.82,1.0),core*0.65);
    } else if(mode==7) {
        float glint=pow(0.5+0.5*sin(t+unitPos.y*1.5),18.0);
        mask=glint*rim*0.06;color=vec3(0.55,0.52,0.45);
    } else if(mode==8) {
        float speck=step(0.985,hash(floor(unitPos*60.0)));
        mask=rim*0.35+speck*0.12*(0.5+0.5*sin(t*3.0+unitPos.y));
    } else if(mode==9) {
        float edge=1.0-smoothstep(0.25,0.29,phaseBand);
        float breakup=step(0.3,hash(vec3(floor(unitPos.y*9.0),floor(t*5.0),seed)));
        mask=edge*0.7+rim*0.10*breakup;
        color=mix(color,vec3(0.72,0.32,1),edge*0.5);
    } else if(mode==10) {
        float height=3.8-mod(t*1.5,4.1);
        float wash=exp(-abs(unitPos.y-height)*13.0);
        float dust=step(0.994,hash(floor(unitPos*40.0+vec3(0,t,0))));
        mask=wash*0.75+dust*0.45;
        color=mix(color,vec3(1,0.95,0.78),wash*0.6);
    } else if(mode==11) {
        vec2 grid=unitPos.xy*2.0;
        float id=hash(vec3(floor(grid),seed));
        float glyph=line(runeDistance(fract(grid)-0.5,id),0.025)*step(0.50,id);
        mask=glyph*(0.55+0.25*sin(t*2.0+id*8.0));
        color=mix(color,vec3(0.7,0.22,1),glyph*0.3);
    } else if(mode==12) {
        float best=10.0;
        for(int side=-1;side<=1;side+=2) {
            float path=float(side)*(0.25+0.14*sin(unitPos.y*4.0))+fbm(unitPos*2.0)*0.05;
            best=min(best,abs(unitPos.x-path));
            float branch=float(side)*(0.85+sin(unitPos.y*3.0)*0.1);
            best=min(best,abs(unitPos.x-branch)+smoothstep(2.4,2.8,unitPos.y));
        }
        float heartbeat=pow(0.5+0.5*sin(t*6.0),12.0)+0.5*pow(0.5+0.5*sin(t*6.0-0.9),20.0);
        mask=(line(best,0.012)*0.65+exp(-best*35.0)*0.12)*(0.25+0.75*heartbeat);
    } else if(mode==13) {
        float plateEdge=step(0.0001,vUV.y)*(1.0-smoothstep(0.015,0.045,vUV.y));
        float pulse=0.75+0.25*sin(t*1.5+unitPos.y);
        mask=plateEdge*0.75*pulse+rim*0.17;
        color=mix(color,vec3(1,0.85,0.35),plateEdge*0.25);
    } else if(mode==14) {
        vec3 flow=unitPos*vec3(4.0,1.5,4.0)+vec3(0,-t*0.32,0);
        float wisp=fbm(flow+fbm(flow*1.7)*1.2);
        float border=line(wisp-0.46,0.018);
        mask=border*0.15+rim*0.12;
        color=vec3(0.15,0.009,0.24);
    } else if(mode==15) {
        vec2 d=unitPos.xy-vec2(u.p[23],u.p[24]);
        float trace=line(circuitDistance(d),0.014);
        float packet=pow(0.5+0.5*sin(length(d)*16.0+t*5.0),14.0);
        float node=line(length(d)-0.085,0.015)+exp(-dot(d,d)*500.0)*0.65;
        float plateEdge=step(0.0001,vUV.y)*(1.0-smoothstep(0.012,0.032,vUV.y));
        mask=trace*(0.35+packet*0.65)+node+plateEdge*0.22;
        color=mix(color,vec3(0.4,0.8,1),node*0.4);
    } else if(mode==16) {
        float plateEdge=step(0.0001,vUV.y)*(1.0-smoothstep(0.012,0.055,vUV.y));
        float hot=0.75+0.25*sin(t*1.2+unitPos.y*2.0);
        float torso=(1.0-smoothstep(0.27,0.43,abs(unitPos.x)))*smoothstep(0.65,0.9,unitPos.y)*(1.0-smoothstep(2.20,2.40,unitPos.y));
        float arms=smoothstep(0.55,0.82,abs(unitPos.x))*(1.0-smoothstep(2.55,2.85,unitPos.y));
        float slots=stripe(unitPos.y*4.0,0.045)*torso;
        float heatZone=smoothstep(0.36,0.62,fbm(unitPos*vec3(3,1.2,3)));
        // Taller moving noise forms restrained upward flame tongues on the armor.
        vec3 flow=unitPos*vec3(8.0,3.0,8.0)+vec3(0,-t*1.7,0);
        float flameNoise=fbm(flow+vec3(fbm(flow*0.6)*1.2,0,0));
        float flames=smoothstep(0.48,0.72,flameNoise);
        float body=smoothstep(0.5,0.8,unitPos.y)*(1.0-smoothstep(2.60,2.85,unitPos.y));
        float armVents=stripe(unitPos.y*5.0+unitPos.z*0.7,0.065)*arms;
        float embers=line(cells(unitPos*7.0).y,0.008)*flames;
        mask=(plateEdge*(0.35+heatZone*0.65+arms*0.65)*0.85+slots*0.8+
              armVents*(0.18+heatZone*0.45)+flames*body*(0.20+arms*0.18)+embers*body*0.20)*hot;
        color=mix(vec3(1.0,0.055,0.002),vec3(1,0.48,0.035),clamp(slots*0.65+flames*0.45+armVents*0.35,0.0,1.0));
    } else if(mode==17) {
        vec2 water=waterDrops(unitPos,t);
        mask=water.x*0.28+water.y*0.025+rim*0.018;
        color=vec3(0.35,0.6,0.78);
    } else if(mode==18) {
        float grains=step(0.94,hash(floor(unitPos*100.0+vec3(sin(t),-t,0))));
        mask=grains*0.12*(0.5+0.5*sin(t+unitPos.y*3.0));
    } else if(mode==19) {
        float glint=pow(0.5+0.5*sin(unitPos.y*2.0+t*0.7),24.0);
        mask=rim*glint*0.08;color=vec3(0.65,0.8,1.0);
    } else if(mode==20) {
        Wood woodValue=wood(unitPos,t);mask=woodValue.seam*0.12;
        color=vec3(0.06,0.28,0.015);
    } else if(mode==21) {
        vec3 c=cells(unitPos*3.0);
        if(u.p[30]>0.5 && c.z<progress*0.8) discard;
        float border=line(c.y,0.012);
        mask=border*(0.55+0.45*sin(t*2.0+c.z*8.0));
    } else if(mode==22) {
        float grid=max(stripe(unitPos.x*10.0,0.018),stripe(unitPos.y*10.0,0.018));
        float height=0.25+mod(t*0.7+2.2,3.2);
        float scan=exp(-abs(unitPos.y-height)*40.0);
        mask=grid*0.045+scan*0.9+rim*0.05;
        color=mix(color,vec3(0.55,1,1),scan*0.6);
    } else if(mode==23) {
        float field=teleportField(unitPos,t);
        if(field<0.0) discard;
        float edge=1.0-smoothstep(0.04,0.32,field);
        float grid=hash(floor(unitPos/0.125)+seed);
        mask=edge*(0.65+grid*0.35);
        color=mix(color,vec3(0.38,0.05,1.0),step(0.7,grid));
    } else if(mode==24) {
        vec2 d=unitPos.xy-vec2(0.12,2.05);
        float field=length(d*vec2(1.0,0.7))*5.0+unitPos.y*0.7+unitPos.z*0.3+fbm(unitPos*2.0)*0.65;
        float contour=stripe(field-t*0.18+sin(unitPos.y*1.8)*0.08,0.028);
        mask=contour*0.55+rim*0.04;
    } else if(mode==25) {
        vec2 d=unitPos.xy-vec2(u.p[23],u.p[24]);
        float radius=length(d),angle=atan(d.y,d.x);
        float distance=abs(sin(angle*5.0+radius*0.7+fbm(unitPos*2.0)*0.3))*radius;
        float stream=line(distance,0.012),packet=pow(0.5+0.5*sin(radius*11.0+t*7.0),10.0);
        float core=exp(-radius*17.0)*(0.7+0.3*sin(t*5.0));
        mask=stream*(0.1+packet*0.65)+core+exp(-distance*40.0)*0.04;
        color=mix(color,vec3(1,0.22,0.12),core*0.6);
    }

    float alpha=clamp(mask*u.p[5]*u.p[6]*vTint.a,0.0,1.0);
    outColor=vec4(color*vTint.rgb*u.p[29],alpha);
}
