// Articulated overshot watermill — rotatable SCAD mechanical diagram.
// Compile one joint mesh with: openscad -D 'part="drive"' -o drive.stl mill.scad
part = "building";

module box(sx, sy, sz) { cube([sx, sy, sz], center = true); }
module cyl(h, r, r2 = undef, fn = 28) {
    cylinder(h = h, r1 = r, r2 = (r2 == undef ? r : r2), center = true, $fn = fn);
}

module building_geom() {
    // Ground floor slab with shaft / stair wells.
    difference() {
        translate([0, 200, 80]) box(6200, 5200, 160);
        translate([0, -900, 80]) cyl(220, 220, fn = 24);
        translate([1450, -200, 80]) cyl(220, 180, fn = 24);
        translate([2200, 1400, 80]) box(900, 1400, 220);
    }
    // Stone floor (visitor / grinding level).
    difference() {
        translate([0, 200, 2760]) box(6200, 5200, 140);
        translate([0, -900, 2760]) cyl(220, 160, fn = 24);
        translate([1450, -200, 2760]) cyl(220, 420, fn = 28);
        translate([2200, 1400, 2760]) box(900, 1400, 220);
    }
    // Bin loft floor.
    difference() {
        translate([0, 200, 5160]) box(6200, 5200, 120);
        translate([0, -900, 5160]) cyl(200, 120, fn = 20);
        translate([2200, 1400, 5160]) box(900, 1400, 200);
        translate([1450, -200, 5160]) box(700, 700, 200);
    }
    // Timber posts and beams (cutaway +Y gallery keeps machinery visible).
    for (x = [-2800, 2800]) for (y = [-2100, 2400])
        translate([x, y, 2700]) box(220, 220, 5400);
    for (z = [1400, 4000]) {
        translate([0, -2100, z]) box(5600, 180, 220);
        translate([0, 2400, z]) box(5600, 180, 220);
        translate([-2800, 200, z]) box(180, 4300, 220);
        translate([2800, 200, z]) box(180, 4300, 220);
    }
    // Solid walls with door / windows; open +Y gallery wall for tour view.
    translate([-3010, 150, 2700]) box(140, 4700, 5200);
    translate([3010, 150, 2700]) difference() {
        box(140, 4700, 5200);
        translate([0, -400, 900]) box(200, 900, 1400);
        translate([0, 900, 3600]) box(200, 900, 1100);
    }
    translate([0, -2310, 2700]) difference() {
        box(6200, 140, 5200);
        translate([0, 0, 1800]) cyl(300, 180, fn = 20); // watershaft bore
        translate([-1600, 0, 3600]) box(900, 200, 1100);
        translate([1600, 0, 3600]) box(900, 200, 1100);
    }
    // Gallery rail on the open +Y side (still walkable through the gap).
    translate([0, 2550, 1100]) box(6200, 80, 80);
    translate([0, 2550, 2900]) box(6200, 80, 80);
    for (x = [-2400, -1200, 0, 1200, 2400]) translate([x, 2550, 2000]) box(80, 80, 1800);
    // Door leaf frame on +X wall.
    translate([2940, -400, 900]) box(40, 820, 1600);
    // Stairs (visitor path ground -> stone floor -> loft).
    for (i = [0:11]) translate([2200, 900 + i * 100, 120 + i * 220]) box(800, 220, 80);
    for (i = [0:10]) translate([2200, 900 + i * 110, 2860 + i * 210]) box(800, 220, 80);
    translate([2500, 1400, 2700]) box(80, 1400, 5200);
    // Roof plates + ridge (partial, keeps loft airy).
    translate([0, 0, 6200]) rotate([0, 0, 0]) box(6600, 200, 180);
    translate([-1600, 200, 6000]) rotate([0, 18, 0]) box(3600, 5600, 80);
    translate([1600, 200, 6000]) rotate([0, -18, 0]) box(3600, 5600, 80);
    // Fixed bedstone + tun shell + meal bin.
    translate([1450, -200, 2780]) cyl(120, 620, fn = 36);
    translate([1450, -200, 2920]) difference() {
        cyl(280, 660, fn = 28);
        cyl(320, 620, fn = 28);
        translate([0, 650, 0]) box(420, 200, 320);
    }
    translate([1450, 700, 2500]) box(700, 500, 900);
    // Hopper frame above the stones (grain chute).
    translate([1450, -200, 3600]) box(700, 700, 60);
    translate([1450, -200, 3900]) box(520, 520, 520);
    translate([1450, -200, 3300]) cyl(420, 70, r2 = 40, fn = 16);
    // Interior workbench and sack pile.
    translate([-1800, 1200, 550]) box(1200, 500, 700);
    translate([-1800, 1200, 950]) box(1000, 360, 80);
    translate([-900, 1600, 350]) box(500, 400, 500);
    translate([-900, 1600, 650]) box(420, 320, 280);
    // Hurst frame around the pit gears.
    translate([-700, -900, 1400]) box(120, 120, 2200);
    translate([700, -900, 1400]) box(120, 120, 2200);
    translate([0, -900, 2500]) box(1500, 120, 120);
    translate([0, -900, 1100]) box(1500, 160, 160);
}

module race_geom() {
    // Mill pond bank, leat walls, wheel pit and stream bed.
    translate([0, -4200, -80]) box(9000, 3600, 200);
    translate([0, 200, -120]) box(9000, 7000, 160);
    translate([0, -3600, 400]) box(3200, 1400, 800); // pond volume shell
    difference() {
        translate([0, -3600, 900]) box(2800, 1200, 200);
        translate([0, -3400, 900]) box(2200, 900, 240);
    }
    // Stone wheel pit under the overshot wheel.
    translate([0, -2500, 200]) box(2400, 2200, 600);
    translate([-1300, -2500, 900]) box(200, 2200, 1800);
    translate([1300, -2500, 900]) box(200, 2200, 1800);
    translate([0, -3600, 1200]) box(2400, 200, 2400);
    // Tail race.
    translate([0, -4800, 100]) box(1600, 1800, 300);
    // Path stones to the gallery.
    for (x = [-1800, -600, 600, 1800]) translate([x, 3200, 40]) box(700, 900, 80);
    // Water surface placeholders (rendered opaque teal).
    translate([0, -3600, 700]) box(2400, 1000, 40);
    translate([0, -4800, 40]) box(1400, 1600, 40);
}

module sluice_geom() {
    // Guillotine / flap gate at the launder lip (hinge at local origin).
    translate([0, 0, -40]) box(900, 40, 80);
    translate([0, 180, -20]) box(860, 320, 40);
    translate([0, 340, 40]) box(820, 40, 160);
    for (x = [-300, 0, 300]) translate([x, 180, 40]) box(40, 300, 40);
}

module drive_geom() {
    // Local origin = watershaft / wheel axle.
    // Watershaft through the wall into the pit.
    rotate([0, 90, 0]) cyl(4200, 90, fn = 24);
    translate([-1400, 0, 0]) rotate([0, 90, 0]) cyl(160, 140, fn = 20);
    translate([1400, 0, 0]) rotate([0, 90, 0]) cyl(160, 140, fn = 20);
    // Overshot waterwheel (rim + buckets + spokes).
    translate([0, -700, 0]) {
        rotate([0, 90, 0]) difference() {
            cyl(280, 1600, fn = 48);
            cyl(320, 1420, fn = 48);
        }
        rotate([0, 90, 0]) cyl(220, 220, fn = 24);
        for (a = [0:30:330]) rotate([a, 0, 0]) {
            translate([0, 0, 780]) box(40, 80, 1280);
            translate([0, 40, 1480]) box(220, 160, 70); // bucket
        }
    }
    // Pitwheel (large bevel / face gear).
    translate([0, 1600, 0]) {
        rotate([0, 90, 0]) cyl(160, 900, fn = 40);
        for (a = [0:12:348]) rotate([a, 0, 0])
            translate([90, 0, 820]) box(120, 70, 160);
        for (a = [0:60:300]) rotate([a, 0, 0]) translate([0, 0, 450]) box(40, 70, 700);
    }
}

module main_geom() {
    // Local origin near wallower; shaft rises through floors.
    cyl(5200, 110, fn = 24);
    // Wallower (bevel pinion meshing pitwheel).
    translate([0, 0, 900]) {
        cyl(140, 320, fn = 28);
        for (a = [0:15:345]) rotate([0, 0, a]) translate([300, 0, 0]) box(90, 55, 120);
    }
    // Great spur wheel.
    translate([0, 0, 2100]) {
        cyl(160, 780, fn = 40);
        for (a = [0:10:350]) rotate([0, 0, a]) translate([740, 0, 0]) box(90, 55, 140);
        for (a = [0:45:315]) rotate([0, 0, a]) translate([380, 0, 0]) box(40, 70, 560);
    }
    // Crown wheel for sack-hoist drive.
    translate([0, 0, 4000]) {
        cyl(120, 420, fn = 32);
        for (a = [0:18:342]) rotate([0, 0, a]) translate([390, 0, 0]) box(70, 45, 100);
    }
}

module runner_geom() {
    // Spindle + stone nut + runner millstone.
    translate([0, 0, -900]) cyl(2000, 55, fn = 20);
    translate([0, 0, -700]) {
        cyl(120, 220, fn = 24);
        for (a = [0:20:340]) rotate([0, 0, a]) translate([200, 0, 0]) box(70, 45, 100);
    }
    // Runner stone with eye and harps (furrow hint).
    difference() {
        cyl(160, 600, fn = 40);
        cyl(200, 70, fn = 16);
    }
    for (a = [0:45:315]) rotate([0, 0, a]) translate([280, 0, 90]) box(260, 30, 20);
    // Rynd / mace bridge.
    translate([0, 0, -40]) box(420, 60, 40);
    translate([0, 0, -40]) box(60, 420, 40);
}

module hoist_geom() {
    // Sack hoist drum and clutch pulley at loft level.
    cyl(220, 180, fn = 24);
    translate([0, 0, 160]) cyl(60, 220, fn = 20);
    translate([0, 0, -160]) cyl(60, 220, fn = 20);
    for (a = [0:60:300]) rotate([0, 0, a]) translate([160, 0, 0]) box(40, 50, 280);
    translate([0, 280, 0]) box(40, 360, 40); // jib arm
    translate([0, 460, -80]) cyl(120, 30, fn = 12);
}

if (part == "building") building_geom();
if (part == "race") race_geom();
if (part == "sluice") sluice_geom();
if (part == "drive") drive_geom();
if (part == "main") main_geom();
if (part == "runner") runner_geom();
if (part == "hoist") hoist_geom();
