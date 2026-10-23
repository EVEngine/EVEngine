#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=2) in vec4 vTint;
layout(location=3) in vec3 vWorldPos;
layout(location=4) in vec3 vCameraPos;
layout(location=5) in vec3 vViewPos;
layout(location=0) out vec4 outColor;
float hash(vec3 p) { p=fract(p*.3183099+vec3(.11,.27,.39));p*=17.;return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }
float noise(vec3 p) {
 vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void main() {
 vec3 p=vWorldPos,n=normalize(vNormal);
 float grain=noise(p*15.)*.5+noise(p*39.)*.25+noise(p*91.)*.25;
 float strata=smoothstep(.43,.55,noise(vec3(p.x*3.,p.y*25.,p.z*3.)));
 float lit=max(0.,dot(n,normalize(vec3(.3,.8,-.6))));
 vec3 c=mix(vec3(.025,.035,.044),vec3(.15,.17,.18),grain);
 c*=.55+lit*.65; c*=.7+strata*.3;
 c+=vec3(.045,.037,.022)*pow(lit,4.)*grain;
 outColor=vec4(.139*c/max(vec3(.001),1.-c),1.);
}
