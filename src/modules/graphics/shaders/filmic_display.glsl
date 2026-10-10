// Fixed filmic SDR transform for the resolved sky-reference display settings.
// Linear Rec.709/D65 input and output; display encoding belongs to the caller.
const vec3 FILM_LUMA = vec3(.2722287168,.6740817658,.0536895174);
const mat3 FILM_RGB_TO_AP1 = mat3(
    0.6130974024, 0.0701937225, 0.0206155929,
    0.3395231461, 0.9163538791, 0.1095697729,
    0.0473794514, 0.0134523986, 0.8698146341);
const mat3 FILM_AP1_TO_RGB = mat3(
    1.7050509926, -0.1302564175, -0.0240033568,
    -0.6217921205, 1.1408047365, -0.1289689761,
    -0.0832588722, -0.0105483190, 1.1529723328);
const mat3 FILM_AP0_TO_AP1 = mat3(
    1.4514393161, -0.0765537734, 0.0083161484,
    -0.2365107469, 1.1762296998, -0.0060324498,
    -0.2149285693, -0.0996759264, 0.9977163014);
const mat3 FILM_AP1_TO_AP0 = mat3(
    0.6954522414, 0.0447945634, -0.0055258826,
    0.1406786965, 0.8596711185, 0.0040252103,
    0.1638690622, 0.0955343182, 1.0015006723);
const mat3 FILM_BLUE = mat3(
    0.9386393778, 0.0000000000, -0.0000000000,
    0.0000000001, 0.8307941330, 0.0000000000,
    0.0613606221, 0.1692058671, 1.0000000001);
const mat3 FILM_BLUE_INVERSE = mat3(
    1.0653748755, -0.0000003456, 0.0000000198,
    0.0000014467, 1.2036635245, 0.0000000212,
    -0.0653710053, -0.2036677199, 0.9999996001);
const mat3 FILM_EXPAND = mat3(
    1.3704123718, -0.0834334917, -0.0257933209,
    -0.3292921877, 1.0970927480, -0.0986257988,
    -0.0636831194, -0.0108613795, 1.2036949526);
vec3 filmicDisplay(vec3 linearColor) {
    vec3 ap1 = FILM_RGB_TO_AP1 * max(linearColor,vec3(0));
    float luminance = dot(ap1,FILM_LUMA);
    vec3 chroma = ap1 / max(luminance,1e-10) - 1;
    float expansion = (1-exp2(-4*dot(chroma,chroma))) * (1-exp2(-4*luminance*luminance));
    ap1 = mix(ap1,FILM_EXPAND*ap1,expansion);
    ap1 = mix(ap1,FILM_BLUE*ap1,.6);
    vec3 ap0 = FILM_AP1_TO_AP0*ap1;
    float lo = min(ap0.r,min(ap0.g,ap0.b)), hi=max(ap0.r,max(ap0.g,ap0.b));
    float saturation = (max(hi,1e-10)-max(lo,1e-10))/max(hi,1e-2);
    float chromaRadius = sqrt(max(0,ap0.b*(ap0.b-ap0.g)+ap0.g*(ap0.g-ap0.r)+ap0.r*(ap0.r-ap0.b)));
    float yc = (ap0.r+ap0.g+ap0.b+1.75*chromaRadius)/3;
    float shape = (saturation-.4)/.2;
    float t = max(1-abs(.5*shape),0);
    float glowGain = .025*(1+sign(shape)*(1-t*t));
    float glow = yc <= .08*2/3 ? glowGain : (yc >= .16 ? 0 : glowGain*(.08/yc-.5));
    ap0 *= 1+glow;
    float hue = (ap0.r==ap0.g && ap0.g==ap0.b) ? 0 :
        degrees(atan(sqrt(3.0)*(ap0.g-ap0.b),2*ap0.r-ap0.g-ap0.b));
    float redWeight = smoothstep(0,1,1-abs(2*hue/135));
    ap0.r += redWeight*redWeight*saturation*(.03-ap0.r)*.18;
    ap1 = max(FILM_AP0_TO_AP1*ap0,vec3(0));
    ap1 = mix(vec3(dot(ap1,FILM_LUMA)),ap1,.96);
    // The 0.18 input/output anchor defines the toe; the shoulder joins the same straight segment.
    const float slope=.88, toeScale=.45, shoulderScale=.78;
    const float toeMatch = log(.18)/log(10.0)-.5*log(.25)*(toeScale/slope);
    const float straightMatch = .45/slope-toeMatch;
    const float shoulderMatch = .26/slope-straightMatch;
    vec3 logarithm = log(max(ap1,vec3(1e-10)))/log(10.0);
    vec3 straight = slope*(logarithm+straightMatch);
    vec3 toe = 2*toeScale/(1+exp((-2*slope/toeScale)*(logarithm-toeMatch)));
    vec3 shoulder = 1.04-2*shoulderScale/(1+exp((2*slope/shoulderScale)*(logarithm-shoulderMatch)));
    toe = mix(straight,toe,lessThan(logarithm,vec3(toeMatch)));
    shoulder = mix(straight,shoulder,greaterThan(logarithm,vec3(shoulderMatch)));
    vec3 blend = 1-clamp((logarithm-toeMatch)/(shoulderMatch-toeMatch),0,1);
    blend = (3-2*blend)*blend*blend;
    ap1 = mix(toe,shoulder,blend);
    ap1 = max(mix(vec3(dot(ap1,FILM_LUMA)),ap1,.93),vec3(0));
    ap1 = mix(ap1,FILM_BLUE_INVERSE*ap1,.6);
    return clamp(FILM_AP1_TO_RGB*ap1,0,1);
}
