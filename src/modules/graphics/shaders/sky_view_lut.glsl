// Reference sky-view parameterization: local Z is radial up, X is camera forward.
// Kept independent of the texture producer so raster/compute producers share it.
float skyAcos(float v) {
    float x=clamp(abs(v),0.0,1.0);
    float p=((-0.0187293*x+0.0742610)*x-0.2121144)*x+1.5707288;
    float a=sqrt(1.0-x)*p;
    return v>=0.0 ? a : PI-a;
}
float skyAtan(float y,float x) {
    float q=min(abs(x),abs(y))/max(max(abs(x),abs(y)),1e-20);
    float s=q*q;
    float a=((0.0872929*s-0.301895)*s+1.0)*q;
    if(abs(y)>abs(x)) a=PI*.5-a;
    if(x<0.0) a=PI-a;
    return y<0.0 ? -a : a;
}
vec2 skyViewUv(vec3 d,float height,float radius) {
    float beta=skyAcos(sqrt(max(0.0,height*height-radius*radius))/height);
    float horizon=PI-beta;
    float zenith=skyAcos(d.z);
    bool ground=sphere(vec3(0,0,height),d,radius).x>=0.0;
    float y=ground ? .5+.5*sqrt(clamp((zenith-horizon)/beta,0.0,1.0))
                   : .5*(1.0-sqrt(clamp(1.0-zenith/horizon,0.0,1.0)));
    vec2 uv=vec2((skyAtan(-d.y,-d.x)+PI)/(2.0*PI),y);
    const vec2 size=vec2(192,104);
    return (uv+.5/size)*size/(size+1.0);
}
vec3 skyViewDirection(vec2 pixel,float height,float radius) {
    vec2 uv=pixel/vec2(191,103);
    float beta=skyAcos(sqrt(max(0.0,height*height-radius*radius))/height);
    float c=uv.y<.5 ? 1.0-2.0*uv.y : 2.0*uv.y-1.0;
    float angle=uv.y<.5 ? (PI-beta)*(1.0-c*c) : PI-beta+beta*c*c;
    float z=cos(angle), r=sqrt(max(0.0,1.0-z*z));
    float longitude=uv.x*2.0*PI;
    return vec3(r*cos(longitude),r*sin(longitude),z);
}
mat3 skyViewBasis(vec3 up) {
    vec3 forward=normalize(vec3(frame.data[28],frame.data[29],frame.data[30]));
    vec3 left;
    if(abs(dot(up,forward))>.999) {
        // Duff basis, expressed in native Y-up coordinates after UE axis conversion.
        float signY=up.y>=0.0 ? 1.0 : -1.0;
        float a=-1.0/(signY+up.y), b=up.x*up.z*a;
        forward=vec3(1.0+signY*a*up.x*up.x,-signY*up.x,signY*b);
        left=vec3(b,-up.z,signY+a*up.z*up.z);
    } else {
        left=normalize(cross(up,forward));
        forward=normalize(cross(left,up));
    }
    return mat3(forward,left,up);
}
vec3 skyViewQuantize(vec3 radiance) {
    radiance=clamp(radiance,vec3(0),vec3(65024,65024,64512));
    vec3 exponent=max(floor(log2(max(radiance,vec3(1e-30)))),vec3(-14));
    vec3 quantum=exp2(exponent-vec3(6,6,5));
    return roundEven(radiance/quantum)*quantum;
}
#ifndef SKY_VIEW_PRODUCER
layout(set=0,binding=1) uniform sampler2D skyViewTable;
vec3 skyViewRadiance(vec3 direction) {
    vec3 origin=vec3(frame.data[16],frame.data[17],frame.data[18])*.001+vec3(0,atmosphere.geometry.x,0);
    float rawHeight=length(origin);
    float height=max(rawHeight,atmosphere.geometry.x+.005);
    if(height>=atmosphere.geometry.y) return skyIntegrateRadiance(direction,false);
    vec3 up=rawHeight>0.0 ? origin/rawHeight : vec3(0,1,0);
    mat3 basis=skyViewBasis(up);
    vec2 pixel=clamp(skyViewUv(transpose(basis)*direction,height,atmosphere.geometry.x)*vec2(192,104)-.5,
                     vec2(0),vec2(191,103));
    ivec2 base=ivec2(floor(pixel));
    vec2 f=fract(pixel);
    vec3 value=vec3(0);
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 p=min(base+ivec2(x,y),ivec2(191,103));
        value+=texelFetch(skyViewTable,p,0).rgb*(x==0 ? 1.0-f.x : f.x)*(y==0 ? 1.0-f.y : f.y);
    }
    return value;
}
#endif
