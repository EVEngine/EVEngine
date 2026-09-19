# Canonical material schema v11

Schema v11 adds the optional `vegetationMotion` object. It stores exactly the
21 TVE 12.6 material-local controls consumed by native vegetation deformation:

`dynamicMode`, `rigidity`, `facing`, `bending`, `bendingSpeed`,
`bendingScale`, `bendingVariation`, `branch`, `rolling`, `branchSpeed`,
`branchScale`, `branchVariation`, `flutter`, `flutterSpeed`, `flutterScale`,
`flutterVariation`, `interaction`, `interactionMask`, `perspectivePush`,
`perspectiveNoise`, and `perspectiveAngle`.

The four mask-like controls (`dynamicMode`, `rigidity`, `facing`, and
`interactionMask`) are finite values in `[0,1]`. The other controls are finite
values in `[0,1000000]`. Unknown members and missing required members are
rejected when the object is present.

The object deliberately excludes field textures, noise, global wind direction,
world origin, clock time, global motion multipliers, field usage/fallback, and
fade distance. Those values belong to the runtime vegetation-field coordinator.
Reading a v11 material restores the local values but leaves GPU motion disabled
until the runtime explicitly attaches the field atlas and noise texture.

`eve.material/10` is the sole migration input. Migration changes only
`schemaVersion`; it does not invent a `vegetationMotion` object. A v10 object
that already contains that member is rejected as unversioned data. Migration is
atomic and preserves all other fields and dependency identity.
