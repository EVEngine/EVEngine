// Articulated inspection boom — rotatable SCAD mechanical diagram.
// Compile one joint mesh with: openscad -D 'part="shoulder"' -o shoulder.stl mechanism.scad
part = "pedestal";

module box(sx, sy, sz) { cube([sx, sy, sz], center = true); }
module cyl(h, r, r2 = undef) {
    cylinder(h = h, r1 = r, r2 = (r2 == undef ? r : r2), center = true, $fn = 28);
}

module pedestal_geom() {
    translate([0, 0, 40]) box(520, 520, 80);
    translate([0, 0, 110]) cyl(60, 170);
    for (a = [0:90:270]) rotate([0, 0, a]) translate([180, 0, 20]) cyl(40, 28);
}

module slew_geom() {
    // Local origin = slew pivot (top of pedestal column).
    translate([0, 0, 40]) cyl(80, 150);
    translate([0, 0, 110]) box(280, 280, 60);
    translate([0, -40, 180]) box(160, 120, 160);
    translate([0, 0, 220]) cyl(40, 70);
}

module shoulder_geom() {
    translate([0, 0, 0]) rotate([0, 90, 0]) cyl(140, 55);
    translate([0, 0, 280]) box(90, 110, 560);
    translate([0, 0, 560]) rotate([0, 90, 0]) cyl(120, 48);
    translate([55, 0, 180]) box(18, 70, 220);
    translate([-55, 0, 180]) box(18, 70, 220);
}

module elbow_geom() {
    translate([0, 0, 0]) rotate([0, 90, 0]) cyl(110, 44);
    translate([0, 0, 290]) box(70, 90, 580);
    translate([0, 0, 580]) rotate([0, 90, 0]) cyl(100, 38);
    translate([40, 0, 200]) box(14, 54, 260);
    translate([-40, 0, 200]) box(14, 54, 260);
}

module wrist_geom() {
    translate([0, 0, 0]) cyl(50, 42);
    translate([0, 0, 70]) box(90, 90, 90);
    translate([0, 0, 130]) cyl(40, 34);
}

module tool_geom() {
    translate([0, 0, 20]) box(70, 40, 40);
    translate([0, 28, 55]) box(18, 70, 18);
    translate([0, -28, 55]) box(18, 70, 18);
    translate([0, 55, 90]) box(14, 18, 50);
    translate([0, -55, 90]) box(14, 18, 50);
}

if (part == "pedestal") pedestal_geom();
if (part == "slew") slew_geom();
if (part == "shoulder") shoulder_geom();
if (part == "elbow") elbow_geom();
if (part == "wrist") wrist_geom();
if (part == "tool") tool_geom();
