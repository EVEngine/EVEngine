// Minimal two-part SCAD for ScadRig sidecar auto-detect tests.
part = "base";
if (part == "base") cube([10, 10, 4], center = true);
if (part == "arm") translate([0, 0, 8]) cube([4, 4, 12], center = true);
