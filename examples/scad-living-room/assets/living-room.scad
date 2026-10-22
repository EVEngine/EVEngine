// Original parametric interior. Millimetres, Z-up. Each part is a PBR batch.
// Rebuild with ../tools/bake.py; color() deliberately isn't used (STL has no color).
part = "plaster";
$fn = 32;
module box(p,s) { translate(p) cube(s, center=true); }
module roundbox(p,s,r=30) {
    translate(p) hull() for(x=[-1,1],y=[-1,1])
        translate([x*(s[0]/2-r),y*(s[1]/2-r),0]) cylinder(h=s[2],r=r,center=true);
}
module rod(p,h,r) { translate(p) cylinder(h=h,r=r,center=true); }
module shell() {
    box([0,3000,1500],[7200,160,3000]);
    // Window opening in left wall, built as real solids around the aperture.
    box([-3600,0,350],[160,6000,700]);
    box([-3600,0,2800],[160,6000,400]);
    for(y=[-2600,2300]) box([-3600,y,1650],[160,800,1900]);
    // Open front/right and partial ceiling reveal the interior from outside.
    box([0,2450,3050],[7200,1200,100]);
}
module sofa() {
    roundbox([300,1740,360],[3000,980,260],100);
    for(x=[-650,300,1250]) roundbox([x,1650,560],[920,850,180],85);
    for(x=[-650,300,1250]) roundbox([x,2100,900],[920,260,650],70);
    for(x=[-1300,1900]) roundbox([x,1740,660],[220,1080,650],65);
}
module cushions() {
    for(x=[-750,1250]) translate([x,1850,850]) rotate([14,0,x<0?-12:12])
        roundbox([0,0,0],[520,190,460],60);
}
module wood() {
    // Individual planks produce real seam shadows and reflections.
    for(x=[-3300:300:3300]) for(y=[-2500:1000:2500])
        box([x,y,-35],[296,996,70]);
    roundbox([150,-350,510],[1650,900,110],140);
    box([2700,1700,560],[720,1900,100]);
    for(y=[1050,2350]) box([2700,y,280],[100,100,500]);
    for(x=[-950,1550],y=[1450,2080]) rod([x,y,160],250,40);
    for(x=[-360,660],y=[-580,-120]) rod([x,y,250],450,42);
    // Floating media shelves and vertical oak acoustic slats.
    for(z=[650,1250,1850]) box([-2550,2680,z],[1400,450,60]);
    for(x=[-1800:90:1800]) box([x,2860,1830],[40,65,1700]);
}
module brass() {
    rod([-2200,1250,20],40,260); rod([-2200,1250,1060],2080,24);
    rod([-2200,1250,2190],40,310);
    for(x=[-800,300,1400]) { rod([x,200,2700],600,12); rod([x,200,2400],80,150); }
    // Sculptural nested table with polished rim.
    rod([1200,-900,320],600,24); rod([1200,-900,620],35,330);
    box([250,2850,1910],[1500,65,1000]);
}
module charcoal() {
    roundbox([-2300,-1000,500],[850,850,220],110);
    roundbox([-2300,-650,850],[850,180,650],75);
    for(x=[-2630,-1970],y=[-1250,-750]) rod([x,y,200],380,25);
    box([250,2800,1910],[1430,60,930]);
    box([2700,1700,650],[400,900,100]);
}
module ceramic() {
    // Hollow sculpted porcelain vase on the upper shelf.
    translate([-2820,2650,1880]) rotate_extrude($fn=64)
        polygon([[0,0],[75,0],[105,25],[135,110],[125,200],[85,265],
                 [55,300],[55,355],[43,355],[43,298],[74,258],[113,196],
                 [123,110],[94,33],[65,14],[0,14]]);
    for(x=[-2850,-2350]) translate([x,2650,1280]) difference() {
        cylinder(h=230,r1=80,r2=55); translate([0,0,20]) cylinder(h=230,r=40);
    }
    translate([280,-350,565]) difference() {
        cylinder(h=120,r1=100,r2=160); translate([0,0,20]) cylinder(h=110,r1=80,r2=145);
    }
    translate([2820,-1800,0]) difference() {
        cylinder(h=470,r1=220,r2=300); translate([0,0,40]) cylinder(h=470,r1=190,r2=270);
    }
}
module leaves() {
    for(a=[0:45:315]) translate([2820,-1800,450]) rotate([0,0,a]) {
        rotate([0,18,0]) cylinder(h=1000,r=12);
        translate([200,0,650]) rotate([0,35,0]) scale([100,35,360]) sphere(1);
    }
}
if(part=="plaster") shell();
if(part=="oak") wood();
if(part=="linen") sofa();
if(part=="terracotta") cushions();
if(part=="brass") brass();
if(part=="charcoal") charcoal();
if(part=="ceramic") ceramic();
if(part=="foliage") leaves();
if(part=="rug") roundbox([0,-300,12],[3500,2700,24],180);
if(part=="shade") translate([-2200,1250,1800]) difference() {
    cylinder(h=450,r1=310,r2=190); translate([0,0,-2]) cylinder(h=455,r1=295,r2=175);
}
if(part=="luminous") {
    for(x=[-800,300,1400]) rod([x,200,2350],20,130);
    box([0,2820,2920],[6500,30,35]); rod([-2200,1250,1830],160,65);
}
if(part=="art") {
    box([250,2755,1910],[1350,12,850]);
    // Relief adds a second layer of warm minimalist artwork.
    for(x=[-100,350,700]) translate([x,2735,1910]) rotate([90,0,0]) cylinder(h=20,r=160);
}

// Two thin glass panes in the existing 4100 x 1900 mm wall aperture.
if(part=="glass") for(y=[-1175,875]) box([-3530,y,1650],[8,2020,1840]);
if(part=="window-frame") {
    for(z=[720,2580]) box([-3520,-150,z],[45,4100,40]);
    for(y=[-2180,-150,1880]) box([-3520,y,1650],[45,40,1900]);
}

// Celadon ring sculpture on the middle shelf, with a matching low pedestal.
if(part=="celadon") {
    roundbox([-2370,2650,1310],[330,160,60],35);
    translate([-2370,2650,1510]) rotate([90,0,0])
        rotate_extrude($fn=64) translate([128,0,0]) circle(r=48,$fn=24);
}
if(part=="art-glass") box([250,2708,1910],[1410,5,910]);
module glass_cup(p) {
    translate(p) difference() {
        cylinder(h=115,r1=49,r2=60,$fn=64);
        translate([0,0,7]) cylinder(h=110,r1=43,r2=54,$fn=64);
    }
}
module tea_pot() {
    difference() {
        translate([0,0,130]) scale([150,125,120]) sphere(1,$fn=48);
        translate([0,0,132]) scale([143,118,115]) sphere(1,$fn=48);
        translate([0,0,222]) cylinder(h=60,r=64,$fn=48);
    }
    // Hollow rising spout and circular glass handle.
    translate([110,0,125]) rotate([0,52,0]) difference() {
        cylinder(h=180,r1=42,r2=25,$fn=48);
        translate([0,0,-1]) cylinder(h=182,r1=35,r2=18,$fn=48);
    }
    translate([-155,0,135]) rotate([90,0,0])
        rotate_extrude($fn=64) translate([85,0,0]) circle(r=12,$fn=20);
    translate([0,0,244]) cylinder(h=12,r1=72,r2=62,$fn=48);
    translate([0,0,269]) sphere(r=19,$fn=32);
    cylinder(h=20,r=85,$fn=48);
}
if(part=="tea-glass") {
    translate([-350,-250,565]) tea_pot();
    for(x=[160,400,640]) glass_cup([x,-620,565]);
}
