// Original EVEngine fixture, dimensions in millimetres.
$fn = 48;
difference() {
    cylinder(h = 60, r = 30);
    translate([0, 0, 4]) cylinder(h = 60, r = 26);
}
translate([28, 0, 30])
    rotate([90, 0, 0])
        difference() {
            cylinder(h = 6, r = 18, center = true);
            cylinder(h = 8, r = 12, center = true);
        }
