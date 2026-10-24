#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D baseTexture;
layout(push_constant) uniform Parameters {
    vec4 frame; // viewport, attack age in seconds, attack variant
    vec4 settings; // dark background preview
} u;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1,0)), f.x),
               mix(hash(i + vec2(0,1)), hash(i + vec2(1)), f.x), f.y);
}
float field(vec2 p) { return noise(p)*0.65 + noise(p*2.07)*0.25 + noise(p*4.1)*0.1; }
// Straight alpha composition, matching the engine's SrcAlpha blend mode.
vec4 over(vec4 a, vec4 b) {
    float alpha = a.a + b.a * (1.0-a.a);
    return vec4((a.rgb*a.a+b.rgb*b.a*(1.0-a.a))/max(alpha,0.00001), alpha);
}
vec4 slash(vec2 p, float age, float flip) {
    if (age < 0.0 || age > 1.25) return vec4(0);
    p.y *= flip;
    // Reference silhouette: a thin left tail, broad right belly and pointed top.
    vec2 q = (p-vec2(-0.22,-0.12))/vec2(1.17,0.68);
    float angle = atan(q.y,q.x);
    float along = (2.85-angle)/4.28;
    float radius = length(q);
    float t = clamp(along,0.0,1.0);
    // Arc length is the shared clock for brush contact, drying and droplet release.
    float reveal = clamp((age-0.025)/0.24,0.0,1.0);
    float strokeAge = age-(0.025+t*0.24);
    float dissolve = smoothstep(0.23,0.86,strokeAge);
    // Broad pressure and wetness fields break up the arc without changing its clock.
    float envelope = pow(max(sin(t*3.14159),0.0),1.1);
    float pressure = mix(0.32,1.5,smoothstep(0.15,0.70,t));
    pressure *= 0.82+0.38*noise(vec2(t*11.0,3.7));
    float width = 0.19*envelope*pressure;
    float centerline = 0.88 + 0.025*sin(t*9.0);
    float radial = radius-centerline;
    float aa = max(fwidth(radial),0.001);
    float gate = smoothstep(0.0,0.014,along)*(1.0-smoothstep(reveal-0.018,reveal,along));
    // Domain-warped bristle coordinates: irregular branching rather than parallel rails.
    float warp = (field(vec2(t*13.0,radius*11.0))-0.5)*0.055;
    warp += (noise(vec2(t*39.0,radius*27.0))-0.5)*0.018;
    float brushR = radial+warp*envelope;
    float clumps = field(vec2(t*8.0,radial*12.0)+18.0);
    float wet = smoothstep(0.24,0.72,clumps+0.12*smoothstep(0.25,0.75,t));
    float erode = smoothstep(dissolve-0.08,dissolve+0.08,
        field(vec2(t*18.0,brushR*64.0)+7.0));
    float ragged = (field(vec2(t*45.0,radius*55.0))-0.5)*0.024;
    ragged += (noise(vec2(t*180.0,radius*210.0))-0.5)*0.009;
    float silhouette = 1.0-smoothstep(width-aa,width+aa,abs(brushR)+ragged*envelope);
    // Sparse, authored-in-parameter-space tears leave coherent dark ink masses.
    // Each tear follows the swing, but has a different span, drift and brush width.
    float paperGap = 0.0;
    for(int tear=0;tear<11;tear++) {
        float seed = float(tear)+64.0;
        float begin = 0.04+hash(vec2(seed,1))*0.57;
        float end = min(begin+0.13+hash(vec2(seed,2))*0.30,0.96);
        float phase = clamp((t-begin)/(end-begin),0.0,1.0);
        float taper = pow(max(sin(phase*3.14159),0.0),1.7);
        float offset = mix(-0.96,0.45,hash(vec2(seed,3)))*width;
        offset += (noise(vec2(t*15.0,seed))-0.5)*0.040*envelope;
        float thickness = mix(0.003,0.019,hash(vec2(seed,4)))*taper;
        thickness *= 0.55+0.85*noise(vec2(t*29.0,seed+3.0));
        float distance = abs(brushR-offset)+(noise(vec2(t*145.0,radial*210.0))-0.5)*0.006;
        float gap = 1.0-smoothstep(thickness,thickness+aa*1.3,distance);
        gap *= smoothstep(begin,begin+0.025,t)*(1.0-smoothstep(end-0.025,end,t));
        gap *= smoothstep(0.18,0.48,noise(vec2(t*37.0,seed+8.0)));
        paperGap = max(paperGap,gap);
    }
    float body = silhouette*(1.0-paperGap*0.96)*gate*erode;
    body *= mix(0.73,0.99,wet);
    // Fine pigment variation stays subtle; it no longer perforates the entire stroke.
    body *= 0.94+0.06*noise(q*230.0);
    // Water spreads farther inward, carrying large irregular dilute pigment patches.
    float washDist = abs(radial+width*0.65+warp*1.4);
    float wash = 1.0-smoothstep(width*0.65,width*1.55+0.025,washDist);
    float washPigment = field(vec2(t*18.0,radius*32.0)+41.0);
    wash *= smoothstep(0.20,0.60,washPigment)*gate*erode*envelope;
    wash *= 0.16+0.32*clumps;
    vec3 ink = mix(vec3(0.10,0.11,0.11),vec3(0.014,0.019,0.022),wet);
    ink *= 0.96+0.04*noise(vec2(t*90.0,radius*120.0));
    vec4 result = over(vec4(ink,body),vec4(0.29,0.30,0.30,wash));
    // Short, uneven dry scuffs replace the previous long concentric filaments.
    for(int strand=0;strand<6;strand++) {
        float seed = float(strand)+21.0;
        float begin = 0.06+hash(vec2(seed,2))*0.60;
        float end = min(begin+0.07+hash(vec2(seed,3))*0.19,0.93);
        float phase = clamp((t-begin)/(end-begin),0.0,1.0);
        float taper = pow(max(sin(phase*3.14159),0.0),1.5);
        float offset = mix(-0.34,-0.15,hash(vec2(seed,4)))*envelope;
        offset += (noise(vec2(t*21.0,seed))-0.5)*0.065*envelope;
        float hairWidth = (0.002+hash(vec2(seed,5))*0.010)*taper;
        float hair = 1.0-smoothstep(hairWidth,hairWidth+aa,abs(radial-offset+warp*0.5));
        hair *= smoothstep(begin,begin+0.02,t)*(1.0-smoothstep(end-0.02,end,t));
        hair *= gate*erode*(0.15+0.45*hash(vec2(seed,6)));
        hair *= smoothstep(0.25,0.55,noise(vec2(t*32.0,seed)));
        result = over(vec4(ink,hair),result);
    }
    // Cropped baked splash region, 8x5 tiles at 30 fps. Match bake_splashes.py bounds.
    // Nearest animation frame avoids transparent ghost copies of moving droplets.
    vec2 splashUV = (p-vec2(-0.20,-1.10))/vec2(1.85,2.10);
    if(all(greaterThanEqual(splashUV,vec2(0))) && all(lessThanEqual(splashUV,vec2(1)))) {
        float index = clamp(floor(age*30.0+0.5),0.0,39.0);
        vec2 atlasSize = vec2(textureSize(baseTexture,0));
        vec2 tileSize = atlasSize/vec2(8,5);
        vec2 pixel = clamp(splashUV*tileSize,vec2(0.5),tileSize-0.5);
        vec2 uv = (vec2(mod(index,8.0),floor(index/8.0))*tileSize+pixel)/atlasSize;
        float coverage = texture(baseTexture,uv).a;
        // Spatial bilinear AA is retained; only temporal cross-fading is removed.
        coverage = smoothstep(0.08,0.82,coverage);
        result = over(vec4(0.025,0.033,0.035,coverage),result);
    }
    return result;
}
void main() {
    vec2 p = (gl_FragCoord.xy-u.frame.xy*0.5)/u.frame.y*2.0;
    float age = u.frame.z;
    vec4 effect = slash(p,age,u.frame.w==1.0 ? -1.0 : 1.0);
    if(u.frame.w>1.5) effect = over(slash(p,age-0.20,-1.0),effect);
    // Subtle pale rim supports dark-scene readability without emissive black ink.
    if(u.settings.x>0.5) effect.rgb = mix(effect.rgb,vec3(0.48,0.59,0.56),0.32);
    outColor = effect * fragColor;
}
