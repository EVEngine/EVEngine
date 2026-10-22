// NEON R / parametric concept supercar
// Units: mm. Nose points toward -X. No external libraries required.
/* [Configuration] */
model_scale = 1; // [0.25:0.05:2]
door_angle = 0; // [0:5:70] Scissor doors; use 55 for display
show_wing = true;
show_wheels = true;
/* [Quality] */
$fn = 64;
/* [Hidden] */
paint = [0.015,0.69,0.76];
carbon = [0.035,0.045,0.06];
glass = [0.075,0.16,0.21];
alloy = [0.65,0.72,0.76];
// Sculpted cross sections: x, half width, bottom, shoulder, crown.
sections = [[-108,32,11,20,22],[-98,43,10,25,28],[-66,46,11,32,35],[-32,42,12,30,34],[15,43,12,31,36],[60,49,11,36,39],[94,46,12,31,34],[103,39,15,27,28]];
module section(s){
 x=s[0]; w=s[1]; b=s[2]; h=s[3]; t=s[4];
 translate([x,0,0]) rotate([0,90,0]) linear_extrude(height=.2,center=true)
 polygon([[-b,-w*.85],[-b,w*.85],[-h,w],[-t,w*.7],[-t,-w*.7],[-h,-w]]);
}
module body_blank(){for(i=[0:len(sections)-2]) hull(){section(sections[i]);section(sections[i+1]);}}
module axle_cylinder(x,y,z,r,h){translate([x,y,z]) rotate([90,0,0]) cylinder(r=r,h=h,center=true);}
module body(){color(paint) difference(){
 body_blank();
 for(x=[-65,65]) for(s=[-1,1]) axle_cylinder(x,s*46,22,23,28);
 // Cut the actual door apertures, retaining structural sills.
 for(s=[-1,1]) translate([-7,s*44,31]) cube([61,14,28],center=true);
 // Deep front air intakes.
 for(y=[-24,0,24]) translate([-105,y,17]) cube([15,y==0?19:15,9],center=true);
}}
module strip(points,r=1){hull() for(p=points) translate(p) sphere(r=r,$fn=16);}
module canopy(){
 color(glass) hull(){
 translate([-35,0,33]) cube([1,65,1],center=true);
 translate([-8,0,55]) cube([1,48,1],center=true);
 translate([23,0,54]) cube([1,49,1],center=true);
 translate([49,0,37]) cube([1,65,1],center=true);
 }
 color(paint) hull(){translate([-7,0,56]) cube([1,49,1.6],center=true);translate([23,0,55]) cube([1,50,1.6],center=true);}
 for(s=[-1,1]) color(paint){
 strip([[-36,s*33,34],[-8,s*25,56]],1.3);
 strip([[23,s*25,55],[49,s*33,38]],1.8);
 }
}
module door(s){
 translate([-34,s*37,30]) rotate([0,-door_angle,0]) translate([34,-s*37,-30]) {
 color(paint) hull(){translate([-31,s*39,23]) cube([1,5,14],center=true);translate([24,s*40,24]) cube([1,5,16],center=true);}
 color(carbon) strip([[-30,s*42,17],[23,s*43,18]],.65);
 color(alloy) translate([15,s*43,30]) cube([7,1,1.3],center=true);
 }
}
module wheel(x,s){
 translate([x,s*45,22]) rotate([90,0,0]) {
 color([.025,.028,.035]) difference(){cylinder(r=21.5,h=14,center=true); cylinder(r=15.7,h=16,center=true);}
 color(carbon) cylinder(r=15.9,h=12,center=true);
 for(side=[-1,1]) translate([0,0,side*7.1]) {
 color(alloy) difference(){cylinder(r=16,h=1,center=true);cylinder(r=14.4,h=2,center=true);}
 color([.28,.32,.35]) translate([0,0,-side*1]) cylinder(r=12.5,h=.6,center=true);
 color([1,.22,.045]) translate([10,0,0]) cube([3,9,2],center=true);
 color(alloy) for(a=[0:72:359]) rotate([0,0,a]) hull(){translate([3,0,side*.8]) cylinder(r=1.3,h=1.7,center=true);translate([13,3,side*.8]) cylinder(r=1,h=1.7,center=true);}
 color(carbon) cylinder(r=4,h=3.5,center=true);
 color(paint) cylinder(r=2,h=4,center=true);
 }
 // circumferential tread channels, represented as raised fine bands
 color([.06,.065,.07]) for(z=[-4,0,4]) translate([0,0,z]) rotate_extrude() translate([21.3,0]) circle(r=.25,$fn=8);
 }
}
module aero(){
 color(carbon){
 hull(){translate([-104,0,9]) cube([16,87,2],center=true);translate([-88,0,9]) cube([8,90,2],center=true);}
 for(s=[-1,1]) hull(){translate([-40,s*43,10]) cube([5,6,3],center=true);translate([40,s*46,10]) cube([5,7,3],center=true);}
 translate([96,0,12]) cube([19,77,5],center=true);
 for(y=[-30:10:30]) translate([98,y,10]) rotate([0,-12,0]) cube([19,1.4,9],center=true);
 }
 // Side intake blades ahead of the rear wheels.
 for(s=[-1,1]){
 color(carbon) hull(){translate([29,s*43,26]) cube([1,2,17],center=true);translate([43,s*46,29]) cube([1,2,12],center=true);}
 color(paint) strip([[27,s*44,17],[42,s*47,32]],1);
 // Wing mirrors
 color(carbon) strip([[-25,s*32,42],[-22,s*47,40]],.9);
 color(paint) translate([-21,s*48,40]) scale([2.9,1.7,1]) sphere(r=2);
 }
 if(show_wing){
 color(carbon) for(s=[-1,1]) translate([83,s*24,43]) rotate([0,-16,0]) cube([4,2.5,24],center=true);
 color(carbon) hull(){translate([82,0,55]) cube([13,108,2.3],center=true);translate([91,0,57]) cube([3,108,2],center=true);}
 color(paint) for(s=[-1,1]) translate([85,s*54,56]) cube([19,2,9],center=true);
 }
}
module detail(){
 // LED signature follows the tapered nose.
 for(s=[-1,1]){
 color([.75,1,1]) strip([[-102,s*26,26],[-94,s*36,28]],.9);
 color(carbon) strip([[-100,s*26,25],[-91,s*38,28]],1.6);
 color([.7,1,1]) strip([[-102,s*26,26.6],[-94,s*36,28.6]],.65);
 color([1,.025,.06]) strip([[102,s*11,26],[100,s*35,28]],.9);
 }
 // Hood extractor vents and twin graphic stripes.
 color(carbon) for(s=[-1,1]) for(i=[0:4]) translate([-69+i*3,s*23,35.2]) rotate([0,-3,s*12]) cube([1.3,13,1],center=true);
 color(carbon) for(y=[-4,4]) hull(){translate([-99,y,28.2]) cube([1,2.7,.5],center=true);translate([-37,y,34.4]) cube([1,2.7,.5],center=true);}
 color(carbon) for(i=[0:6]) translate([58+i*4,0,39-i*.19]) cube([1.6,41,1],center=true);
 for(y=[-14,14]) color(alloy) translate([104,y,18]) rotate([0,90,0]) difference(){cylinder(r=3,h=3,center=true);cylinder(r=2.2,h=4,center=true);}
 color(alloy) translate([-101,0,28.5]) linear_extrude(.4) polygon([[-2,0],[0,-2],[2,0],[0,3]]);
}
scale(model_scale){body(); canopy(); door(-1); door(1); aero(); detail(); if(show_wheels) for(x=[-65,65]) for(s=[-1,1]) wheel(x,s);}
