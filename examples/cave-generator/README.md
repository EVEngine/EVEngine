# Karst Lab - Procedural Cave Generator

Interactive showcase for the `procgen` module's deterministic `mesh.cave` recipe.
The camera starts inside the generated cave so inward-facing walls render with the
same winding used by gameplay cameras.

## Run

```sh
make run/win32-debug GAME=examples/cave-generator
make run/linux-debug GAME=examples/cave-generator
make run/macosx-debug GAME=examples/cave-generator
```

## Controls

- `R`: advance the deterministic seed.
- `1`: broad linked caverns.
- `2`: narrow tunnel networks.
- `3`: vertical shafts and rising passages.
- `4`: winding labyrinth passages.
- `5`: mixed cave system.
- `6`: accessible-microporosity limestone with distributed dissolution.
- `7`: heterogeneous limestone with competing channels.
- `8`: high-contrast, wormhole-prone limestone.

The example generates the CPU mesh with `buildMesh("mesh.cave", params)`, checks
the returned Result, uploads it once, and keeps the GPU mesh between frames. Its
default preset enables bedding-controlled notches, joint-guided dissolution,
solution pockets, and a narrow vadose stream incision beneath the older phreatic
passage. Curvature-weighted outer-bank undercut breaks axial symmetry in winding
passages and creates broad recirculation alcoves beneath the smaller flow-scaled
scallops. A normalized maturity control blends residual fine cells with coarsened
wide troughs and narrow cusp-like ridge networks. A seeded correlated scale field
approximates the log-normal length variability measured by Springer and Hall
(2020), while a slope-sensitive phase warp gives mature cells a steeper upstream
face and gentler downstream face following Fowler's 2025 travelling-wave and
flow-separation model. Both controls remain disabled in the recipe defaults.
A second, spatially partitioned scallop stage now preserves a readable erosion
history instead of assuming one stationary discharge. Coherent wall bands gain
smaller high-flow cells, and only long passage reaches may reverse phase after
drainage capture or backflooding; the older relief is never restored. The design
uses the multiple base/high-flow velocity populations and stratigraphic perching
reported by the 2025 Black Canyon field study at the National Cave and Karst
Management Symposium, while the centimetre-scale structural context follows the
2025 Grand Canyon cave LiDAR study. This is a cumulative morphology proxy rather
than a dated flood hydrograph, and `scallopFlowHistory` defaults to zero.
Joint-guided dissolution is also spatially non-uniform in this preset. Correlated
aperture patches modulate each structural plane, while stress-controlled open
windows split and reconnect into competing dissolution branches. This follows
the front-splitting and wormhole transition reported by Jiang et al. (2025,
DOI `10.1029/2024JB029901`), but remains a bounded morphology proxy rather than
a coupled stress, contact-mechanics, and reactive-transport solve. Both joint
controls are disabled in the recipe defaults.
The preset also closes the causal loop between those aperture patches and
dissolution. Wider patches receive a cubic conductance advantage, while the
existing Damkohler-derived penetration length limits reactant delivery farther
downstream; crossings of two open joints gain a bounded extra enlargement. This
follows the nonlinear aperture-flow-dissolution feedback measured by Xu et al.
(2025, DOI `10.3389/feart.2025.1701477`) and the fracture-network sensitivity
reported by Aliouache et al. (2025, DOI `10.1016/j.jhydrol.2024.131684`). The
implementation is a deterministic wall-morphology proxy, not a time-resolved
flow, stress, or solute solver, and `fractureFlowFeedback` defaults to zero.
Three descending epiphreatic levels add laterally continuous, gently wandering
corrosion belts to the sidewalls. They represent the migrating high-dissolution
fringe predicted by the 2025 distributed-recharge fracture-network model and
the multi-level water-table notches observed at Acqua Fitusa. This stage is
separate from sediment-driven paragenesis and is inactive for purely hypogene
caves.
Branch-to-trunk junctions receive a separate, compact mixing-corrosion overprint.
Every site reuses the hydrology network's actual branch anchor and estimates its
mixing potential from the two passage capacities plus a seeded chemistry
contrast. The result is a modest confluence alcove, not an extra random chamber;
this bound follows both the 2025 hydro-thermal-chemical mixing model and the
classic finding that mixing corrosion alone does not produce a large conduit.
Layered host rock now contributes a separate selective-erosion signal. Seeded
bed packages retain coherent resistance over distance, while only clustered,
flow-accessible contacts widen into thin stylolite-guided grooves. This avoids
both voxel-white-noise lithology and a perfectly repeated notch on every bed.
The model follows the field relationship between stylolite thickness/spacing
and cave geometry reported for the Salitre Formation, and couples it to the
flow-heterogeneity control identified across 29 carbonate dissolution datasets.
It is a bounded mesoscopic morphology proxy, not a mineral-specific reactive
transport solver.
Sediment-laden floods add a separate mechanical-erosion stage near the passage
bed. Hydraulic exposure sets stream power, coherent recirculation cells localize
scour and pothole-like depressions, and sediment load follows a tools-and-cover
response: mobile grains abrade the rock, while excessive cover increasingly
shields it. The near-bed decay and bounded event-scale retreat follow the 2024
Hollental limestone-gorge LiDAR observations; the vortex-cell structure follows
the 2026 open-channel pothole CFD study. This is intentionally inactive for a
purely hypogene cave and is not presented as free-surface CFD or particle DEM.
Flood plucking adds sparse angular recesses where strong flow reaches exposed
joint-bounded rock. The sampler keeps the two strongest local fracture signals,
uses their overlap as block predisposition, and releases only seeded cuboid cells
that cross a hydraulic threshold. This changes erosion location and roughness
rather than uniformly accelerating every wall, matching the central constraint
from the 2025 fractured-bed erosion-mill experiments. The current cave joints are
near-vertical proxies, so this stage does not claim full dip-angle mechanics,
fracture-toughness propagation, or rigid-body transport of the removed blocks.
Passage-scale erosion also follows actual local radius minima instead of placing
another class of random chambers. Each sufficiently strong constriction drives a
downstream-shifted, vertically biased scour pool; its exit receives a broader
lateral-retreat lobe. This constriction-pool-widening sequence follows Kusack et
al.'s 2024 flume experiment (DOI `10.1029/2024JF007808`) and the field synthesis
by Ross et al. (2026, DOI `10.5194/esurf-14-553-2026`). The normalized proxy does
not solve a free surface, transient discharge, or sediment storage, and it is
disabled for purely hypogene caves.
The response is deliberately non-monotonic: the 2026 large-eddy simulations by
Samarasinghe et al. (arXiv `2607.16908`) place peak plunging-flow efficiency near
35% constriction at lower flow and near 50% at higher flow, with concentrated
bed stress at the pool entrance. Tighter passages therefore do not receive
unbounded extra scour.
The old single-scale wall displacement is blended almost entirely into a
three-band, band-limited relief spectrum. Large, medium, and resolvable fine
features therefore coexist without turning crystal-scale topography into noisy
metre-scale spikes. The frequency-space treatment follows the PSD framing of
Zhou and Fischer's 2025 sequential calcite topographies and the explicitly
multi-resolution geometry published in the 2025 KarstConduitCatalogue.
It also generates asymmetric
stalactite/stalagmite pairs using conical,
columnar, and flat-topped steady forms, with a small seeded chance of joined
columns. A host-rock connectivity pass removes fully detached erosion fragments
while retaining wall-, floor-, and ceiling-supported formations. Deposits are
formed after a curvature-driven normal-retreat pass rounds exposed convex rock
asperities without diffusing sheltered recesses or the directional scallop ridges.
The retreat rate is spatially bounded but heterogeneous: accessible reactive
microstructure and hydraulic exposure accelerate some wall patches while
transport-limited regions persist, without turning unresolved crystal-scale rate
contrasts into metre-scale noise.
The final wall now also receives a seeded two-band correlated reactivity field.
The nearest hydrology frame supplies a normalized flow tangent, stretching the
field along each trunk or branch rather than leaving isotropic blobs in world
space. This turns heterogeneous dissolution into coherent flow-aligned etch
patches and residual terraces rather than independent voxel noise, while
iterative retreat gives the more reactive patches a bounded widening feedback.
The spatial-spectrum framing
follows Zhou and Fischer's 2025 PSD analysis of 80 sequential calcite surface
topographies, while the accessibility and channel-widening behavior follows Ma
et al.'s 2026 time-resolved micro-CT and pore-scale reactive-transport study
(DOI `10.1016/j.advwatres.2025.105202`). This remains a resolvable-scale morphology
proxy, not a two-phase flow, concentration, or crystal-step solver, and
`reactivePatchiness` defaults to zero.
The channelization constraint is also consistent with Hyman et al.'s 2026 3D
fracture-network study (DOI `10.1029/2025JB033004`); no Reynolds flow, particle
tracking, or breakthrough curve is solved here.
Weakly flushed ceiling sectors receive a separate seeded condensation-corrosion
overprint. It upscales long-term cool-wall, CO2-bearing water-film attack into
bounded shallow pits; it does not claim to resolve the measured micrometre-scale
film or reaction layer directly.
Secondary-mineral armoring supplies the missing negative feedback. Seeded,
passage-coherent coating patches suppress chemical retreat most strongly in
sheltered sectors, while high hydraulic exposure strips much of the rind and
keeps fast-flow paths reactive. The dense-coating limit follows the order-of-
magnitude CaCO3 rate reduction measured in the 2025 limestone microfluidic
shielding experiments (DOI `10.1016/j.bgtech.2025.100186`); the 2026
multimineral flow study (DOI `10.1029/2025WR042362`) supplies the broader
constraint that mineral arrangement and proximity to fast channels govern
effective reaction rates. `mineralArmoring` is a bounded morphology control,
not a sulfate concentration or precipitation-kinetics solver, and defaults to
zero.
The showcase additionally enables localized planar corrosion facets. A slowly
varying humidity/convection field blends a five- to seven-plane passage-local
envelope into side walls and selected ceiling sectors, breaking uniformly round,
blobby cross-sections without turning the whole conduit into a regular polygon.
This normalized morphology is informed by the planar condensation-corrosion
facets reported by Audra et al. (Geomorphology 492, 2026, article 110054); their
roughly 15 cm wall-retreat observation is a qualitative scale reference rather
than a claim that this generator solves cave airflow or heat transfer.
Localized resistant-vein weathering adds a second structure-dependent signal.
Two warped intersecting mineral-vein sets protect narrow cores while adjacent
host rock retreats, leaving discontinuous boxwork ribs instead of painted-on or
additive decoration. This follows the same 2026 study's observation of Mn/Fe-rich
veins standing several centimetres proud after differential erosion. The opt-in
control is a morphological proxy for caves with suitable mineralized veins, not
a mineral precipitation or rock-mechanics simulation.
Five deterministic breakdown events connect chemical weakening to mechanical
shape change. Each event removes one shallow oriented ceiling slab and deposits
two to four related slab-like or blocky fragments on the chamber floor. The
paired source prevents context-free random rubble, while a dedicated `breakdown`
triangle group gives landed rock a darker, rougher material. This is a bounded
stability-informed morphology, not a stress solve or rigid-body fall simulation.
Five low-relief sediment bars then reuse the generated trunk paleoflow instead of
inventing a second direction field. Fine deposits elongate downstream; flattened
gravel clasts place their long axes across the flow and dip upstream in an
imbricated fabric. The separate dark-brown `sediment` group keeps transported
material visually distinct from limestone breakdown. This is a facies-informed
morphology rather than a transient flood or discrete-particle simulation.
Those bars also gate a shallow paragenetic overprint: only sediment-filled
segments enlarge upward into flow-aligned antigravitative ceiling channels.
With deposition disabled the same control is strictly inactive, rather than
inventing ceiling grooves without a stratigraphic cause. The effect follows the
sediment-constrained upward enlargement described by Holzer et al. (2025) and
the paleoflow role of ceiling channels reported by Sevil-Aguareles et al. (2025);
its rounded, gently meandering half-tube uses the equilibrium width trend from
Cooper and Covington's cross-section evolution model (2020). Greater sediment
supply raises but weakly narrows the channel. It is not a groundwater-level
history or reactive-transport simulation.
The active cave stream also derives knickpoints from the generated trunk's actual
longitudinal profile. A sufficiently steep downstream slope break produces an
elongated plunge pool below the lip and weaker lower-headwall undercut; ordinary
undulations remain untouched. Hydraulic exposure and a non-monotonic sediment
tools-versus-cover response control the magnitude. This morphology proxy follows
Davy et al.'s 2026 coupled bedrock-incision model, Hiramatsu et al.'s 2024
waterfall-migration experiments, and Scheingross et al.'s 2017 self-formed plunge
pools. It does not simulate a transient free surface or a migration chronology.
Exposed stream beds additionally reuse the two strongest generated fracture sets
to form crossing solution grooves and deeper junction pockets. The effect follows
the fracture-guided floor karren mapped in the 2025 KarstConduitCatalogue's Markov
Spodmol scan and the streamwise groove selection observed in flowing-film
dissolution experiments. It remains inactive on the roof, in weak flow, and when
the rock lacks two crossing fracture sets; it is a morphology proxy rather than a
thin-film reactive-transport solve.
At crossing-fracture weak zones, the same active stream can now develop sparse
eddy potholes. Fine grinder gravel broadens abrasion toward the downstream wall
and bed, while coarse gravel produces a tighter upstream-biased cut; sufficiently
energetic sites add a smaller compound pit at the base. These constraints follow
Sumner and Inoue's 2026 hydraulic experiments and a 2026 three-dimensional
secondary-flow study. The generator does not scatter circular dents independently
of structure and does not claim to solve individual gravel trajectories.
Landed ceiling-breakdown blocks now feed back into the stream morphology instead
of remaining context-free props. Blocks inside the active trunk footprint create
a deeper two-lobed horseshoe scour on the upstream side and a longer, shallower
wake trough downstream. Existing multi-scale bed roughness damps the coherent
vortex response. The relative geometry follows 2025–2026 PIV, flume, and CFD–DEM
boulder studies; it does not treat mobile-sand equilibrium depths as limestone
incision rates or add a sediment ridge without a matching depositional material.
The same resolved wall relief now also participates in carbonate mass transfer.
Convex ridges receive a bounded increase in chemical retreat, while recessed,
recirculating pockets are sheltered; stronger hydraulic exposure partly flushes
those pockets. This opt-in proxy follows 2026 rough-fracture flow observations and
carbonate reactive-transport simulations (DOIs `10.1016/j.rineng.2026.110345` and
`10.3390/min16010110`). It affects chemical dissolution only and is not a CFD or
concentration-field solve.
After the first chemical pass, the showcase also measures the geometry that was
actually produced rather than relying only on its source noise. Neighboring SDF
normals provide a rotation-invariant dispersion proxy: real edges, corners, and
rough slopes receive more retreat, while planar walls remain unchanged. Normals
are recomputed between iterations so morphology feeds back into reactivity. This
follows the roughness-based surface-reactivity parameterization presented at
InterPore 2026, while using a voxel-normal approximation rather than claiming its
point-covariance `Rq` metric or resolving crystal-scale defects.
The remaining bars are treated as remnants below a higher palaeofill surface.
A narrow, laterally continuous wall notch marks that former interface, while
untouched rock between neighboring roof half-tubes remains as solution pendants.
This linked morphology follows the sediment-speleogenesis sequence reviewed by
Farrant and Smart (2011), the combined notch/channel evidence measured by
Sevil-Aguareles et al. (2025), and the persistent level-bound corrosion notches
reported from Sa Gleda Cave in 2026. No decorative pendant mesh is added.
The showcase also opts into a tropical biogenic overprint based on 2025 Mulu
Cave observations: passage-aligned airflow replaces some small fluvial scallops
with broad megascallops, fluting, and rounded spongework. Fast wall films retain
more of the older scallops. The recipe default remains disabled because this
process requires guano, warm humid conditions, and ventilation.
Deposits are
exposed as the `speleothems` triangle group and rendered
with a lighter, smoother calcite material than the limestone walls.
Drainage-adjacent lower surfaces are emitted as `wetWalls` with a darker,
lower-roughness material. Group-aware vertex-normal smoothing reduces Marching
Cubes faceting without changing topology or blending across material seams.
Two-times trilinear field supersampling reconstructs the continuous zero level
set more densely before adaptive Newton-projected refinement, reducing coarse
silhouette stair-steps while preserving every original scalar sample.
The showcase then derives surface normals from that continuous density field,
so lighting follows the erosion interface instead of the incidental triangle
split pattern; the original face-average mode remains the recipe default.
Its wet/dry material boundary is also clipped against a continuous,
gravity-directed drainage field after geometric refinement. This keeps the
named `wetWalls` group while avoiding whole-triangle moisture patches; it does
not exaggerate the micrometre-scale water film into a separate mesh shell.
For an interior showcase, a seeded rough host-rock envelope closes passages
that reach the finite density-domain boundary. The recipe default remains open
so games can retain entrances, exits, or stitch neighboring cave chunks.
Wall-attached flowstone patches and wavy ceiling curtains approximate cumulative
carbonate deposition driven by gravity-drained thin water films.
Keys `6` through `8` keep the same hydrology while changing accessible
microporosity and permeability contrast, demonstrating that rock microstructure
alone can switch between distributed retreat and localized conduits.

The first stable rendered frame is saved to `build/cave-debug/cave-generator.png`
for engine-owned visual QA; the build directory is ignored by Git.
