// Original Type 055 display model, approximate public silhouette.
// Units: millimetres at 1:1000 (180 mm overall). X forward, Z up.
// Set part to hull / fittings / dark / markings for separate material exports.
part = "all";
$fn = 24;
// Longitudinal layout envelopes, shared by structures and clearance checks.
aft_vls_x = -38;
aft_vls_pitch = 1.6;
aft_vls_lid = 1.32;
hangar_x = -50;
hangar_length = 20;
aft_funnel_x = -21;
aft_funnel_length = 8;
aft_vls_min = aft_vls_x - aft_vls_lid/2;
aft_vls_max = aft_vls_x + 5*aft_vls_pitch + aft_vls_lid/2;
assert(aft_vls_min - (hangar_x + hangar_length/2) >= 1,
       "Aft VLS must clear the hangar by at least 1 mm");
assert((aft_funnel_x - aft_funnel_length/2) - aft_vls_max >= 1,
       "Aft VLS must clear the funnel by at least 1 mm");
module box(p,s) { translate(p) cube(s,center=true); }
module taper(p, bottom, top, h) {
    translate(p) hull() {
        linear_extrude(0.02) square(bottom,center=true);
        translate([0,0,h]) linear_extrude(0.02) square(top,center=true);
    }
}
// Continuous station loft: broad transom, fine entry and raised bow.
stations=[[-90,7.5,5],[-78,9.6,5.2],[-50,10,5.5],[-15,10,5.8],
          [25,9.4,6.2],[53,7.5,6.8],[72,4.3,7.6],[86,1.4,8.2],[90,0.12,8.4]];
function deck_height(x,i=0) =
    i >= len(stations)-2 || x <= stations[i+1][0]
    ? stations[i][2] + (stations[i+1][2]-stations[i][2]) *
      (x-stations[i][0])/(stations[i+1][0]-stations[i][0])
    : deck_height(x,i+1);
module section(s) {
    translate([s[0],0,0]) rotate([0,90,0]) linear_extrude(0.04)
        polygon([[-s[2],-s[1]],[-s[2],s[1]],[-0.4,s[1]*0.78],
                 [3,s[1]*0.42],[3,-s[1]*0.42],[-0.4,-s[1]*0.78]]);
}
module hull_body() {
    for(i=[0:len(stations)-2]) hull() { section(stations[i]); section(stations[i+1]); }
}
module fittings() {
    // Angular forward deckhouse and bridge wings.
    taper([12,0,5.6],[34,17],[30,13.7],7.7);
    taper([13,0,13],[24,13.8],[20,11.6],5.2);
    taper([13,0,18.2],[22,13.8],[18.7,11.2],1.2);
    // Integrated mast with four planar sensor faces.
    taper([1,0,17],[10,9],[7,6],9);
    taper([1,0,26],[7.7,6.5],[4.4,4],4);
    box([1,0,32],[0.65,0.65,5]);
    box([1,0,29.5],[10,0.45,0.4]);
    box([-1,0,27.5],[0.4,10,0.4]);
    // Two shrouded funnels, aft deckhouse and helicopter hangar.
    taper([-11,0,5.7],[10,14],[8,11],10.3);
    taper([-11,0,16],[8,10],[7,8],3);
    taper([aft_funnel_x,0,5.6],[aft_funnel_length,14],[7,11],10.4);
    taper([aft_funnel_x,0,16],[7,10],[6,8],3);
    taper([hangar_x,0,5.4],[hangar_length,17.2],[18,14.7],7);
    // Dedicated flat coamings above the sheer deck, below each VLS lid.
    box([-34,0,5.85],[10.4,13.3,0.5]);
    box([40.775,0,6.75],[13.4,13.4,0.18]);
    // Forward 130 mm gun, low polygon stealth enclosure.
    translate([61,0,7]) cylinder(h=0.8,r=3.2);
    taper([61,0,7.6],[6.5,5.8],[4.2,4],3.3);
    translate([63,0,9.7]) rotate([0,78,0]) cylinder(h=10,r1=0.47,r2=0.28);
    // CIWS and aft close-in launcher (display geometry only).
    translate([29,0,14]) cylinder(h=1,r=2);
    taper([29,0,15],[3.4,3.3],[2.8,2.6],2);
    for(y=[-0.35,0.35]) translate([30,y,16]) rotate([0,90,0]) cylinder(h=3,r=0.18);
    taper([-50,0,12.4],[4,4],[3,3],2);
    translate([-50,0,15.3]) rotate([0,-15,0]) cube([4,5,1.8],center=true);
    // Radomes, navigation equipment and antenna whips.
    for(p=[[-21,0,20.3],[20,0,20.5]]) translate(p) sphere(r=1.4);
    for(y=[-5,5]) { box([-53,y,16.4],[0.22,0.22,8]); box([18,y,23],[0.18,0.18,6]); }
    // Liferaft canisters and deck bollards.
    for(x=[-55,-51,-47], y=[-8,8]) translate([x,y,8]) rotate([90,0,0]) cylinder(h=1.5,r=0.65,center=true);
    for(x=[-85,-72,74],side=[-1,1]) {
        y = side*(x > 0 ? 2.6 : 5);
        z = deck_height(x);
        box([x,y,z+0.12],[1.8,0.8,0.3]);
        for(dx=[-0.5,0.5]) translate([x+dx,y,z+0.2]) cylinder(h=0.8,r=0.22);
    }
    // Fine aft safety rails, interrupted at the transom opening.
    for(y=[-7.2,7.2]) {
        for(x=[-88:3:-58]) box([x,y,deck_height(x)+0.75],[0.16,0.16,1.6]);
        hull() for(x=[-88,-58]) box([x,y,deck_height(x)+1.5],[0.16,0.16,0.16]);
    }
}
module dark_details() {
    // Individual VLS lids: 64 forward, 48 aft.
    for(x=[0:7],y=[0:7]) box([35+x*1.65,-5.8+y*1.65,6.9],[1.38,1.38,0.18]);
    for(x=[0:5],y=[0:7]) box([aft_vls_x+x*aft_vls_pitch,-5.6+y*1.6,6.18],[aft_vls_lid,aft_vls_lid,0.18]);
    // Bridge glazing arranged across the front and both wings.
    for(y=[-4.8:1.6:4.8]) box([23.05,y,17.2],[0.22,1.25,1.05]);
    for(y=[-6.35,6.35],x=[8:2:20]) box([x,y,17.15],[1.5,0.18,0.9]);
    // Hangar door and its slatted ribs.
    box([-59.5,0,8.3],[0.2,9.5,4.7]);
    for(z=[6.4:0.55:10.3]) box([-59.65,0,z],[0.1,9.2,0.12]);
    for(y=[-4.55,4.55]) box([1,y,23],[4.8,0.16,3.5]);
    box([5.05,0,23],[0.16,4.5,3.5]);
    for(x=[-11,-21],y=[-3,0,3]) box([x,y,19.05],[5,1.3,0.12]);
}
module markings() {
    // Flight deck perimeter, landing circle, centre line and H.
    translate([-73,0,5.72]) linear_extrude(0.09) {
        difference() { circle(r=6,$fn=64); circle(r=5.8,$fn=64); }
        for(y=[-1.4,1.4]) translate([0,y]) square([4,0.22],center=true);
        square([0.22,2.8],center=true);
        for(x=[-12:3:12]) translate([x,0]) square([1.5,0.15],center=true);
        for(y=[-7.2,7.2]) translate([0,y]) square([28,0.15],center=true);
    }
    // Raised pennant numbers on both bows.
    for(side=[-1,1]) translate([69,side*5.05,4.8]) rotate([90,0,side==1?180:0])
        linear_extrude(0.12) text("101",size=2.1,halign="center",font="Liberation Sans:style=Bold");
}
if(part=="all" || part=="hull") color([0.46,0.53,0.57]) hull_body();
if(part=="all" || part=="fittings") color([0.67,0.72,0.74]) fittings();
if(part=="all" || part=="dark") color([0.13,0.20,0.24]) dark_details();
if(part=="all" || part=="markings") color([0.91,0.91,0.81]) markings();
