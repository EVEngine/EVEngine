# Same-mesh concept generation

Mode: built-in imagegen, stylized-concept. These are aspirational reference images,
not replacements for actual engine captures. Source baseline: armored-baseline.png.
The generated references are saved alongside it as pilot-same-mesh.png and
same-mesh-26.png. The original concept-26.png remains the earlier art direction.

## Invariants supplied to imagegen

Use the exact neutral mesh and camera in the input screenshot: rounded helmet,
angular visor and central mask, layered chest/abdomen armor, shoulder plates and
short armored forearms. Preserve pose, geometry, angle, scale and very dark navy
background. Change only surface materials and attached effects. Do not redesign
the robot or add legs/anatomy. Retain neutral metal between luminous motifs.
Use controlled HDR cores, saturated colored edges and soft bloom. No floor circles
or huge particle clouds. Clearly a CONCEPT TARGET, never an engine screenshot.

## Pilot prompt set

3 columns x 2 rows, header SAME-MESH PILOT REFERENCES, labels in order:
01 SHIELD, 02 HIT, 07 SHOCK, 21 BARK, 24 TELEPORT, 27 BASE.

- Shield: sparse consistent cyan hex grid over helmet/chest/arms, blue silhouette.
- Hit: limited orange-white chest impact, branching hot cracks, radial fading ring.
- Shock: 2–3 irregular branching blue-white arcs over shoulders/chest, no sine stripes.
- Bark: rough brown wood fibers, grooves, knots and relief; sparse subdued green seams.
- Teleport: lower torso/forearms disappearing at an ascending cyan-violet block boundary,
  a few small drifting square fragments, preserved recognizable upper body.
- Base: the neutral input material with no effect.

## Full prompt set

7 columns x 4 rows, 28 cells, strict row-major numbering. Header SAME-MESH UNIT
SURFACE REFERENCES - 26 EFFECTS. Use all model/lighting invariants above.

01 cyan sparse hex shield; 02 localized orange-white chest hit plus ring;
03 ice-blue crystalline veins and matte frost patches; 04 char with sparse molten cracks;
05 dark corrosive islands with lime boundaries; 06 mint ascending bands and few crosses;
07 irregular branching shoulder/chest electricity; 08 rough stone patches and dark cracks;
09 almost transparent body with faint teal silhouette; 10 broken violet bands and real gaps;
11 white-gold descending wash and fine speckles; 12 scattered angular purple runes;
13 red vascular heartbeat veins; 14 gold borders following armor plates;
15 creeping black-violet surface wisps; 16 blue octilinear circuits to one chest node;
17 dark metal with red-orange hot zones/vents; 18 wet dark metal and downward rivulets;
19 granular ochre sand crust and swirled erosion; 20 silver faceted reflective material;
21 rough bark, knots/fissures and subtle green seams; 22 dark polygon islands and magenta cracks;
23 faint cyan technical grid and one strong scan line; 24 rising cyan-violet pixel breakup;
25 continuous blue wrapping contour ripples; 26 crimson streams converging on one chest center;
27 neutral baseline; 28 legend SURFACE / UNIT SPACE / ANIMATED.
