#!/usr/bin/env bash
# Bootstrap KayKit CC0 character + three motion libraries used by this example.
#
# Prefer the already-verified in-tree fixture from combat-action-editor (no network).
# Override with KAYKIT_SRC=/path/to/kaykit if you keep a local KayKit checkout.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
DST="$ROOT/assets/kaykit"
SRC="${KAYKIT_SRC:-$ROOT/../combat-action-editor/assets/kaykit}"

FILES=(
  Knight.glb
  knight_texture.png
  Rig_Medium_MovementBasic.glb
  Rig_Medium_CombatMelee.glb
  Rig_Medium_General.glb
  LICENSE-KAYKIT-ADVENTURERS.txt
  LICENSE-KAYKIT-ANIMATIONS.txt
)

if [[ ! -d "$SRC" ]]; then
  echo "KayKit source not found: $SRC" >&2
  echo "Set KAYKIT_SRC to a directory containing the KayKit Adventurers + Animations CC0 files," >&2
  echo "or run this from a full EVEngine checkout that includes examples/combat-action-editor." >&2
  exit 1
fi

mkdir -p "$DST"
for f in "${FILES[@]}"; do
  if [[ ! -f "$SRC/$f" ]]; then
    echo "missing $SRC/$f" >&2
    exit 1
  fi
  cp -f "$SRC/$f" "$DST/$f"
  echo "fetched $f"
done

cat > "$DST/README.md" <<'EOF'
# KayKit assets for layered-animation

CC0 KayKit Adventurers + Character Animations subset, bootstrapped by
`../fetch_assets.sh` from `examples/combat-action-editor/assets/kaykit`.

Clips used by the demo:

| File | Clip | Role |
|------|------|------|
| `Rig_Medium_MovementBasic.glb` | `Walking_A` | locomotion base |
| `Rig_Medium_CombatMelee.glb` | `Melee_1H_Attack_Chop` | upper-body override |
| `Rig_Medium_General.glb` | `Hit_A` | additive hit |

Character mesh: `Knight.glb` + `knight_texture.png`.
EOF

echo "OK -> $DST"
