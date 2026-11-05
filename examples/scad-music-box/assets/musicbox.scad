// Articulated cylinder music box — rotatable SCAD mechanical diagram.
// Compile one joint mesh with: openscad -D 'part="cylinder"' -o cylinder.stl musicbox.scad
part = "shell";

module box(sx, sy, sz) { cube([sx, sy, sz], center = true); }
module cyl(h, r, r2 = undef, fn = 28) {
    cylinder(h = h, r1 = r, r2 = (r2 == undef ? r : r2), center = true, $fn = fn);
}

module shell_geom() {
    // Ornate wooden shell with cutaway front so the mechanism stays readable.
    difference() {
        translate([0, 0, 28]) box(180, 120, 56);
        translate([0, -8, 36]) box(156, 88, 48);
        translate([0, -62, 40]) box(140, 20, 36); // front window
    }
    // Feet.
    for (x = [-70, 70]) for (y = [-42, 42])
        translate([x, y, 4]) cyl(8, 8, fn = 16);
    // Inner shelf for the dancer platform.
    translate([0, -28, 54]) box(50, 40, 4);
    translate([0, -28, 48]) cyl(8, 6, fn = 16);
    // Hinge knuckles on the back wall.
    for (x = [-40, 0, 40]) translate([x, 55, 58]) rotate([0, 90, 0]) cyl(16, 4, fn = 12);
}

module lid_geom() {
    // Local origin = hinge axis. Lid body opens toward +Z / -Y when rotated.
    translate([0, -55, 6]) box(176, 112, 8);
    // Beveled glass well (opaque panel stand-in).
    translate([0, -55, 2]) box(140, 80, 4);
    // Front lip.
    translate([0, -108, 10]) box(160, 8, 12);
    // Hinge barrels.
    for (x = [-40, 0, 40]) rotate([0, 90, 0]) translate([0, 0, x]) cyl(14, 5, fn = 14);
}

module bed_geom() {
    // Brass bedplate with axle supports.
    translate([0, 0, 2]) box(150, 90, 4);
    // Cylinder axle stands.
    translate([-55, -6, 18]) box(10, 18, 32);
    translate([70, -6, 18]) box(10, 18, 32);
    translate([-55, -6, 28]) rotate([0, 90, 0]) cyl(8, 5, fn = 14);
    translate([70, -6, 28]) rotate([0, 90, 0]) cyl(8, 5, fn = 14);
    // Comb mount rail.
    translate([8, 22, 8]) box(110, 14, 10);
    // Barrel well.
    translate([-58, 8, 8]) cyl(8, 22, fn = 28);
    // Governor tower.
    translate([62, 10, 14]) cyl(20, 6, fn = 16);
}

module barrel_geom() {
    // Mainspring barrel + winding arbor.
    cyl(16, 20, fn = 36);
    translate([0, 0, 12]) cyl(6, 22, fn = 36);
    translate([0, 0, -12]) cyl(6, 22, fn = 36);
    for (a = [0:30:330]) rotate([0, 0, a]) translate([18, 0, 0]) box(6, 3, 14);
    translate([0, 0, 18]) cyl(12, 4, fn = 12);
    translate([0, 0, 26]) box(18, 4, 4); // winding key stub
}

module cylinder_geom() {
    // Local origin = drum axle. Drum + end flanges + tune pins.
    rotate([0, 90, 0]) cyl(100, 22, fn = 40);
    translate([-52, 0, 0]) rotate([0, 90, 0]) cyl(6, 26, fn = 32);
    translate([52, 0, 0]) rotate([0, 90, 0]) cyl(6, 26, fn = 32);
    // Drive gear at +X end.
    translate([58, 0, 0]) rotate([0, 90, 0]) {
        cyl(8, 16, fn = 28);
        for (a = [0:15:345]) rotate([a, 0, 0]) translate([0, 0, 14]) box(4, 3, 6);
    }
    // Pin schedule (one chorus).
    rotate([0.0, 0, 0]) translate([-42.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([15.0, 0, 0]) translate([-42.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([30.0, 0, 0]) translate([6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([45.0, 0, 0]) translate([6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([60.0, 0, 0]) translate([18.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([75.0, 0, 0]) translate([18.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([90.0, 0, 0]) translate([6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([120.0, 0, 0]) translate([-6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([135.0, 0, 0]) translate([-6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([150.0, 0, 0]) translate([-18.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([165.0, 0, 0]) translate([-18.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([180.0, 0, 0]) translate([-30.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([195.0, 0, 0]) translate([-30.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([210.0, 0, 0]) translate([-42.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([240.0, 0, 0]) translate([6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([255.0, 0, 0]) translate([6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([270.0, 0, 0]) translate([-6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([285.0, 0, 0]) translate([-6.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([300.0, 0, 0]) translate([-18.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([315.0, 0, 0]) translate([-18.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([330.0, 0, 0]) translate([-30.0, 0, 24]) box(3.2, 3.2, 10);
    rotate([345.0, 0, 0]) translate([-30.0, 0, 24]) box(3.2, 3.2, 10);
}

module comb_geom() {
    // Tuned comb: longer (lower) teeth toward -X.
    translate([0, 0, 2]) box(110, 12, 4);
    for (i = [0:7]) {
        length = 28 + (7 - i) * 4;
        translate([-42 + i * 12, -length * 0.5, 0]) box(8, length, 2.4);
        translate([-42 + i * 12, -length, 0]) box(6, 4, 3);
    }
}

module governor_geom() {
    // Air-brake fan / worm governor.
    cyl(10, 5, fn = 16);
    translate([0, 0, 8]) cyl(4, 7, fn = 16);
    for (a = [0:90:270]) rotate([0, 0, a]) {
        translate([0, 14, 10]) box(3, 22, 1.6);
        translate([0, 24, 10]) box(10, 6, 1.2);
    }
}

module dancer_geom() {
    // Compact ballerina figurine (skirt + torso + arms).
    translate([0, 0, 2]) cyl(4, 10, fn = 24);
    translate([0, 0, 10]) cyl(14, 16, r2 = 4, fn = 28); // skirt
    translate([0, 0, 22]) cyl(12, 4, fn = 16);
    translate([0, 0, 30]) cyl(8, 5, fn = 16); // head
    translate([-10, 0, 24]) rotate([0, 55, 0]) box(16, 3, 3);
    translate([10, 0, 24]) rotate([0, -55, 0]) box(16, 3, 3);
}

if (part == "shell") shell_geom();
if (part == "lid") lid_geom();
if (part == "bed") bed_geom();
if (part == "barrel") barrel_geom();
if (part == "cylinder") cylinder_geom();
if (part == "comb") comb_geom();
if (part == "governor") governor_geom();
if (part == "dancer") dancer_geom();
