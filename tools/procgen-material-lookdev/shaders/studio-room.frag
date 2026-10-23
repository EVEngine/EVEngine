#version 450
// Six real walls form the same visible and reflected environment.
layout(location=3) in vec3 vWorldPos;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform Controls {float values[32];} control;
float panel(vec3 direction,vec2 extent,vec3 ray){
 vec3 axis=normalize(direction),right=normalize(cross(axis,vec3(0,1,0))),up=cross(right,axis);
 float facing=dot(ray,axis);
 if(facing<=0)return 0;
 vec2 uv=vec2(dot(ray,right),dot(ray,up))/facing;
 return (1-smoothstep(extent.x-.015,extent.x,abs(uv.x)))*
        (1-smoothstep(extent.y-.015,extent.y,abs(uv.y)));
}
void main(){
 vec3 p=vWorldPos;
 vec3 base=vec3(.105,.11,.12);
 if(p.y<control.values[1]+.2){
  float checker=mod(floor(p.x/1.5)+floor(p.z/1.5),2);
  base=mix(vec3(.055,.060,.065),vec3(.19,.18,.16),checker);
 }else if(p.y>control.values[4]-.2){base=vec3(.16,.17,.18);}
 else if(p.z<control.values[2]+.2){
  vec2 cell=fract((p.xy+vec2(10,9))/vec2(2.4,2.7));
  float frame=1-smoothstep(.025,.045,min(min(cell.x,1-cell.x),min(cell.y,1-cell.y)));
  float pane=mod(floor((p.x+10)/2.4)+floor((p.y+9)/2.7),2);
  base=mix(mix(vec3(.15,.18,.20),vec3(.24,.20,.15),pane),vec3(.025),frame);
 }else if(p.x<control.values[0]+.2){base=vec3(.16,.12,.09);}
 else if(p.x>control.values[3]-.2){base=vec3(.09,.13,.18);}
 else if(p.z>control.values[5]-.2){
  float frame=step(.45,abs(fract(p.x*.22)-.5));
  base=mix(vec3(.14,.15,.16),vec3(.025),frame);
 }
 vec3 ray=normalize(p);
 float key=panel(vec3(control.values[6],control.values[7],control.values[8]),vec2(control.values[9],control.values[10]),ray);
 float rim=panel(vec3(control.values[12],control.values[13],control.values[14]),vec2(control.values[15],control.values[16]),ray);
 // HDR radiance is preserved in the reflection probe's floating-point faces.
 vec3 radiance=base+vec3(control.values[11])*key+vec3(control.values[17])*rim;
 float dither=fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)-.5;
 outColor=vec4(radiance+vec3(dither/255.0),1);
}
