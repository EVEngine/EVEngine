#version 450
// Analytic dielectric laboratory, rendered through the Mesh3D custom shader path.
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Parameters { float data[32]; } pc;
const float EPS = 0.001;

// Light cards and room surfaces compete by distance; a wall can no longer
// overwrite a nearer emitter. Radiance remains linear HDR until engine resolve.
void card(vec3 p, vec3 d, vec3 center, vec3 axisU, vec3 axisV,
          vec2 halfSize, vec3 emission, inout float nearest, inout vec3 color) {
    vec3 normal=cross(axisU,axisV);
    float denom=dot(normal,d);
    if(abs(denom)<1e-5) return;
    float t=dot(center-p,normal)/denom;
    if(t<=EPS || t>=nearest) return;
    vec3 q=p+t*d-center;
    vec2 uv=abs(vec2(dot(q,axisU),dot(q,axisV)))/halfSize;
    if(max(uv.x,uv.y)>1.0) return;
    // Feather emitter borders to keep narrow highlights stable.
    float edge=1.0-smoothstep(0.88,1.0,max(uv.x,uv.y));
    color=emission*edge; nearest=t;
}
vec3 studio(vec3 p, vec3 d, bool showLightCards) {
    vec3 color=mix(vec3(0.008,0.012,0.022),vec3(0.09,0.13,0.20),smoothstep(-0.2,1.0,d.y))*pc.data[10];
    float nearest=1e5;
    if(d.y < -1e-5) {
        float t=(-1.5-p.y)/d.y;
        if(t>EPS) {
            nearest=t; vec3 q=p+t*d;
            color=mix(vec3(0.025,0.034,0.05),vec3(0.10,0.13,0.17),exp(-dot(q.xz,q.xz)*0.025));
        }
    }
    if(d.z < -1e-5) {
        float t=(-5.0-p.z)/d.z;
        if(t>EPS && t<nearest) {
            nearest=t; vec3 q=p+t*d;
            float glow=exp(-dot(q.xy*vec2(0.16,0.25),q.xy*vec2(0.16,0.25)));
            color=mix(vec3(0.012,0.022,0.04),vec3(0.16,0.24,0.34),glow);
            // One broad architectural band shows bent rays without a dense checker.
            color+=vec3(0.18,0.23,0.28)*(1.0-smoothstep(0.15,0.3,abs(q.y-0.25)));
        }
    }
    if(!showLightCards) return color;
    float angle=pc.data[9], c=cos(angle), sn=sin(angle);
    mat3 rotation=mat3(c,0,-sn, 0,1,0, sn,0,c);
    vec3 x=rotation*vec3(1,0,0), y=vec3(0,1,0), z=rotation*vec3(0,0,1);
    float size=pc.data[8];
    card(p,d,rotation*vec3(-3.4,1.8,3.5),x,y,vec2(1.1,2.8)*size,
         vec3(1.0,0.90,0.76)*pc.data[5],nearest,color);
    card(p,d,rotation*vec3(3.4,1.0,3.0),x,y,vec2(0.45,2.6)*size,
         vec3(0.60,0.80,1.0)*pc.data[6],nearest,color);
    card(p,d,vec3(0,4,0),x,z,vec2(4.5,1.5)*size,
         vec3(0.85,0.93,1.0)*pc.data[7],nearest,color);
    card(p,d,vec3(0,1.5,-4.8),vec3(1,0,0),y,vec2(3.8,0.3)*size,
         vec3(0.65,0.83,1.0)*pc.data[11],nearest,color);
    return color;
}
// Bounded deterministic cone filter for low roughness. This is an environment
// blur approximation, not a microfacet GGX BSDF or a path tracer.
vec3 filteredStudio(vec3 p, vec3 d) {
    float radius=pc.data[3]*pc.data[3];
    if(radius<1e-5) return studio(p,d,true);
    vec3 tangent=normalize(cross(abs(d.y)<0.95?vec3(0,1,0):vec3(1,0,0),d));
    vec3 bitangent=cross(d,tangent);
    vec3 sum=vec3(0);
    for(int i=0;i<4;i++) {
        float angle=(float(i)+0.5)*1.57079633;
        sum+=studio(p,normalize(d+radius*(cos(angle)*tangent+sin(angle)*bitangent)),true);
    }
    return sum*0.25;
}

// Convex octahedron: intersection of eight half-spaces |x|+|y|+|z| <= radius.
// Entry/exit normals come from the same planes used to compute thickness.
bool intersectSolid(vec3 p, vec3 d, int shape, out float entry, out float exitT,
                    out vec3 entryN, out vec3 exitN) {
    entry = -1e5; exitT = 1e5;
    entryN = vec3(0,1,0); exitN = entryN;
    if(shape == 0) {
        float b=dot(p,d), c=dot(p,p)-1.0, disc=b*b-c;
        if(disc < 0.0) return false;
        entry=-b-sqrt(disc); exitT=-b+sqrt(disc);
        entryN=normalize(p+entry*d); exitN=normalize(p+exitT*d);
        return exitT > EPS;
    }
    if(shape == 2) {
        // Six rotated cube planes; keep the hit normal planar on each face.
        const float angle = 0.45;
        mat3 rotation = mat3(cos(angle),0,-sin(angle), 0,1,0, sin(angle),0,cos(angle));
        for(int axis=0;axis<3;axis++) for(int side=0;side<2;side++) {
            vec3 n=vec3(0); n[axis]=side==0?-1.0:1.0; n=rotation*n;
            float denom=dot(n,d), distance=0.85-dot(n,p);
            if(abs(denom)<1e-6) { if(distance<0.0) return false; continue; }
            float t=distance/denom;
            if(denom<0.0) { if(t>entry) { entry=t; entryN=n; } }
            else if(t<exitT) { exitT=t; exitN=n; }
        }
        return exitT > max(entry,EPS);
    }
    for(int i=0;i<8;i++) {
        vec3 n=normalize(vec3((i&1)==0?-1:1,(i&2)==0?-1:1,(i&4)==0?-1:1));
        float denom=dot(n,d), distance=1.35/sqrt(3.0)-dot(n,p);
        if(abs(denom)<1e-6) { if(distance<0.0) return false; continue; }
        float t=distance/denom;
        if(denom<0.0) { if(t>entry) { entry=t; entryN=n; } }
        else if(t<exitT) { exitT=t; exitN=n; }
    }
    return exitT > max(entry,EPS);
}
float fresnel(float cosine, float etaI, float etaT) {
    float sinT2=pow(etaI/etaT,2.0)*max(0.0,1.0-cosine*cosine);
    if(sinT2>=1.0) return 1.0;
    float ct=sqrt(1.0-sinT2);
    float rs=(etaI*cosine-etaT*ct)/(etaI*cosine+etaT*ct);
    float rp=(etaT*cosine-etaI*ct)/(etaT*cosine+etaI*ct);
    return 0.5*(rs*rs+rp*rp);
}
float dielectric(vec3 origin, vec3 ray, vec3 center, int shape, int channel) {
    float a,b; vec3 n,m;
    intersectSolid(origin-center,ray,shape,a,b,n,m);
    vec3 p=origin+ray*a;
    float ior=max(1.001,pc.data[0]+float(1-channel)*pc.data[2]);
    float f=fresnel(max(dot(-ray,n),0.0),1.0,ior);
    float result=f*filteredStudio(p,reflect(ray,n))[channel];
    float throughput=1.0-f;
    vec3 d=refract(ray,n,1.0/ior);
    p+=d*EPS;
    // Follow the internal reflected branch; accumulate transmitted light at every exit.
    for(int bounce=0;bounce<12;bounce++) {
        if(!intersectSolid(p-center,d,shape,a,b,n,m)) break;
        // attenuationColor is transmittance at attenuationDistance, in world units.
        float attenuation=max(pc.data[12+channel],0.001);
        throughput*=exp(log(attenuation)*b/max(pc.data[4],0.001)*pc.data[1]);
        p+=d*b;
        float reflectance=fresnel(max(dot(d,m),0.0),ior,1.0);
        vec3 transmitted=refract(d,-m,ior);
        if(reflectance<0.99999) result+=throughput*(1.0-reflectance)*filteredStudio(p,transmitted)[channel];
        throughput*=reflectance;
        d=reflect(d,m); p+=d*EPS;
        if(throughput<0.001) break;
    }
    return result;
}
vec3 traceScene(vec3 ray) {
    float nearest=1e5; int selected=-1;
    for(int i=0;i<3;i++) {
        vec3 center=vec3(float(i-1)*2.7,0,0);
        float a,b; vec3 n,m;
        if(intersectSolid(vCameraPos-center,ray,i==0?2:(i==1?1:0),a,b,n,m) && a>EPS && a<nearest) {
            nearest=a; selected=i;
        }
    }
    vec3 color=studio(vCameraPos,ray,false);
    if(selected>=0) {
        vec3 center=vec3(float(selected-1)*2.7,0,0);
        int shape=selected==0?2:(selected==1?1:0);
        // All three shapes share exactly the same dielectric parameters and path.
        for(int c=0;c<3;c++) color[c]=dielectric(vCameraPos,ray,center,shape,c);
    }
    return color;
}
void main() {
    // Four fixed subpixel rays, averaged before the single engine display transform.
    vec3 dx=dFdx(vWorldPos), dy=dFdy(vWorldPos), sum=vec3(0);
    for(int i=0;i<4;i++) {
        vec2 offset=vec2((i&1)==0?-0.25:0.25,(i&2)==0?-0.25:0.25);
        sum+=traceScene(normalize(vWorldPos+dx*offset.x+dy*offset.y-vCameraPos));
    }
    outColor=vec4(sum*0.25,1);
}
