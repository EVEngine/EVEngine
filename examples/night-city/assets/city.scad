// Original reusable city kit. Metres, Z-up; bake.py converts to Y-up.
part = "tower";
$fn = 12;
module block(p, s) { translate(p) cube(s, center=true); }
module tower(floors=8) {
    height=floors*2.8+1.6;
    block([0,0,0.65],[12,12,1.3]);
    block([0,0,height*0.5+0.5],[10.8,10.8,height]);
    block([0,0,height+1],[9.4,9.4,1]);
    block([0,0,height+2.3],[4,5,1.6]);
    for(z=[2:2.8:height]) block([0,0,z],[11.1,11.1,0.14]);
    for(x=[-5.4,5.4]) for(y=[-5.4,5.4]) block([x,y,height*0.5+0.5],[0.24,0.24,height]);
}
module windows(lit,floors=8) {
    for(f=[0:floors-1]) for(c=[0:5]) for(side=[0:3])
        if (((f*17+c*7+side*13)%11<7)==lit)
            rotate([0,0,90*side]) block([-4.4+c*1.76,-5.415,3.0+f*2.8],[1.1,0.035,1.65]);
}
module lamp() {
    cylinder(h=0.3,r1=0.32,r2=0.22);
    translate([0,0,0.3]) cylinder(h=6.5,r1=0.105,r2=0.065);
    block([0.85,0,6.65],[1.8,0.14,0.14]);
    block([1.65,0,6.56],[0.9,0.42,0.18]);
}
module signal() {
    cylinder(h=0.2,r=0.22);
    cylinder(h=3.8,r=0.075);
    block([0,0,3.45],[0.5,0.32,1.42]);
    for(z=[2.99,3.45,3.91]) block([0,-0.24,z+0.13],[0.55,0.38,0.07]);
}
if(part=="tower") tower();
if(part=="lit") windows(true);
if(part=="dark") windows(false);
if(part=="tower-high") tower(16);
if(part=="lit-high") windows(true,16);
if(part=="dark-high") windows(false,16);
if(part=="lamp") lamp();
if(part=="lamp-glow") block([1.65,0,6.455],[0.76,0.34,0.025]);
if(part=="signal") signal();
if(part=="lens") rotate([90,0,0]) cylinder(h=0.025,r=0.17,center=true,$fn=16);
