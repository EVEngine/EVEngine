# KayKit combat action editor assets

This directory contains a deliberately small, unmodified subset of two free
KayKit packs by Kay Lousberg. The runnable example and regression test use the
same files to exercise EVEngine's real glTF, skeletal-animation, combat timeline,
and action-preview paths without requiring network access.

Sources:

- [KayKit Adventurers 2.0 FREE](https://kaylousberg.itch.io/kaykit-adventurers)
- [KayKit Character Animations 1.1 FREE](https://kaylousberg.itch.io/kaykit-character-animations)

Both packs are licensed CC0 1.0. The original license files are preserved as
`LICENSE-KAYKIT-ADVENTURERS.txt` and `LICENSE-KAYKIT-ANIMATIONS.txt`. No paid,
EXTRA, or SOURCE-tier content is included.

Original archives downloaded from the official itch.io pages on 2026-08-28:

- `KayKit_Adventurers_2.0_FREE.zip` SHA-256
  `abe48f4763fba0896bab486ee9e6d08ca6b5b3884b9601f235c8847ae94dc479`
- `KayKit_Character_Animations_1.1_FREE.zip` SHA-256
  `65882f31f905ad2e953819648a59287cdeab8f623908d5ef701971d3758be20f`

Selected content:

- `Knight.glb` and the one-handed sword glTF fixture
- Medium-rig General, Movement Basic, Movement Advanced, Combat Melee, and
  Combat Ranged animation libraries

The files remain in their original coordinate system and are not presented as
EVEngine-created artwork. `examples/combat-action-editor/main.nut` is the visual
integration example and `test/kaykit_combat_assets.cpp` provides deterministic
regression coverage.
