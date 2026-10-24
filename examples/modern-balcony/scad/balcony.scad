// Authoring coordinates: millimetres, Z-up. Runtime conversion: (x,z,-y)/1000.
// Export each material group with -D 'part="stone"'. Empty selects full scene.
part = "";
$fn=24;
module box(p,s) { translate(p) cube(s,center=true); }
module pot(x,y,r,h) { translate([x,y,0]) difference() {
 cylinder(h=h,r1=r*0.78,r2=r); translate([0,0,35]) cylinder(h=h,r1=r*0.68,r2=r-25);
} }
module stone() {
 box([0,0,-140],[8000,6000,240]);
 for(x=[-3500:1000:3500],y=[-2500:1000:2500]) box([x,y,-8],[987,987,24]);
 box([-3970,0,1550],[150,6000,3100]);
 box([0,2940,110],[8000,120,220]);
}
module metal() {
 for(x=[-3900:1300:3900]) box([x,2900,650],[35,35,1300]);
 box([0,2900,1300],[7900,48,48]);
 for(y=[-2250:1500:2250]) box([3900,y,650],[35,35,1300]);
 box([3900,0,1300],[48,5800,48]);
 for(x=[-3850,3850]) box([x,2600,1500],[70,70,3000]);
 box([0,2600,3000],[7800,90,90]);
 // Slender table base, sofa legs, lounge chair frame.
 box([350,350,350],[95,95,700]);
 box([350,350,35],[700,550,70]);
 for(x=[-2700,-1100],y=[300,1700]) box([x,y,170],[60,60,340]);
 for(x=[1200,2200],y=[700,1800]) box([x,y,250],[45,45,500]);
}
module glass() {
 for(x=[-3250:1300:3250]) box([x,2900,730],[1250,12,1080]);
 for(y=[-1500:1500:1500]) box([3900,y,730],[12,1450,1080]);
}
module wood() {
 // Wall slats and bench sofa structure.
 for(y=[-2700:140:2600]) box([-3860,y,1550],[70,65,2900]);
 box([-1900,1000,350],[1900,1600,120]);
 box([-2800,1000,650],[120,1600,520]);
 box([-1900,1750,700],[1900,100,650]);
 box([1700,1300,450],[1200,1300,100]);
 box([1700,1900,830],[1200,90,800]);
 for(x=[-3750:180:3750]) box([x,2600,3020],[70,700,90]);
 // Front of planted border.
 for(x=[-3550:120:3550]) box([x,2400,310],[95,75,620]);
 box([350,350,720],[1300,1000,65]);
}
module fabric() {
 box([-1880,980,475],[1650,1350,180]);
 box([-1880,1600,840],[1650,200,650]);
 box([1700,1270,550],[1100,1120,140]);
 box([1700,1790,840],[1100,150,550]);
}
module ceramic() {
 pot(-3050,2150,300,800); pot(3050,2150,350,650);
 pot(3000,-1600,260,560); pot(-3200,-1700,210,450);
 pot(750,300,100,200);
}
module soil() {
 box([0,2680,420],[7200,400,120]);
 for(p=[[-3050,2150,270,755],[3050,2150,320,600],[3000,-1600,230,510],[-3200,-1700,180,400],[750,300,75,160]])
 translate([p[0],p[1],p[3]]) cylinder(h=15,r=p[2]);
}
if(part=="" || part=="stone") stone();
if(part=="" || part=="metal") metal();
if(part=="" || part=="glass") glass();
if(part=="" || part=="wood") wood();
if(part=="" || part=="fabric") fabric();
if(part=="" || part=="ceramic") ceramic();
if(part=="" || part=="soil") soil();
