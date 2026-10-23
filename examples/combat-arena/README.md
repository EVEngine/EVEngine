# CIRCUIT / Ability Lab

A playable 3D demonstration of EVEngine's GAS-style building blocks. Two armored
training robots stand on an illuminated arena. The HUD exposes abilities,
attributes, active effects and the native damage result while you play.

## Run

```bash
make run/linux-debug GAME=examples/combat-arena
# or
make run/win32-debug GAME=examples/combat-arena
```

The build needs `graphics`, input, `action`, `combat`, `attributes`, `effects` and `font`.
These providers are available in the minimal profile. The example selects its
runtime modules explicitly. The bundled DejaVu font license is in
`assets/fonts/NOTICE.txt`.

## Controls

| Input | Behavior |
| --- | --- |
| WASD | Move the cyan robot; face the target automatically |
| J | Vector Slash: 15 energy, 2s cooldown, base damage x1 |
| K | Breaker: 40 energy, 4.5s cooldown, base damage x2.4 |
| L | Overdrive: 30 energy, 14s cooldown, +50% attack for 12s |
| C | Cancel the active ability; a cancelled windup cannot hit |
| U | Revoke/re-grant the heavy ability, like unequipping/equipping a weapon |
| H | Hot-replace heavy between standard and 1.8s charged-windup definitions |
| F | Training control: refresh an existing Overdrive while keeping its effect ID |
| V | Training control: dispel all effects from both subjects |
| I | Toggle native modifier-source and execution-snapshot inspection |
| R | Reset runtime, attributes, effects and the automatic demonstration |

At startup, the sample automatically performs a normal slash, applies Overdrive,
then performs an empowered heavy strike. Movement or a skill key stops the
remaining automatic sequence. The target starts at 200 HP; a normal slash deals
12 and applies the first Exposed effect. The empowered heavy then deals
47.52 with that 10% vulnerability, leaving 140.48 HP. Walking beyond 2.7 units makes
an attack miss. Energy regenerates at 4 per second, up to 100.

Every landed hit adds an independent 8s `State.Exposed` effect to the target.
Each effect adds 0.1 to the target's native DamageTaken attribute; at most three
instances are retained, replacing the oldest when full. Their durations, events,
IDs, tags and payloads come from EffectContainer; the three-instance limit is an
explicit game-adapter policy. Expiry or dispel removes each modifier by source.

Active heavy grants cannot be revoked; cancel first or wait for completion.
Re-grant creates a new identity and fresh cooldown state. Definition replacement
preserves existing grants, cooldowns and active execution snapshots; the newly
selected windup and cooldown apply to future activations. Energy paid at admission
is not refunded by cancellation. F and V are clearly marked training controls,
not extra cost-bearing player abilities.

## What the GAS demonstration actually uses

| Concept | Implementation |
| --- | --- |
| Ability definition | Versioned JSON Action timelines in `data/`, registered through `registerTimelineAbility` |
| Grant and activation | Native `ActionRuntime` delegates to `AbilityRuntime`; CombatRuntime owns movement/damage |
| Gameplay event tags | Each input resolves `Ability.Attack.Slash`, `Ability.Attack.Heavy` or `Ability.Buff.Overdrive` through `matchingAbilities` |
| Execution and cooldown | Native windup/active/recover phases, per-execution instances, exclusive-blocking group and cooldown snapshots |
| Attributes | Native `AttributeSet` owns Energy and Attack; HUD reads base/final values |
| Gameplay effect | Native `EffectContainer` owns Overdrive's 12s lifecycle and `State.Empowered` tag |
| Attribute modifiers | Effect payloads project x1.5 Attack and additive target DamageTaken modifiers; removal/expiry events remove each source |
| Cancellation | `cancelAbility` retires the activation; `findAction` exposes the retained terminal execution snapshot |
| Grant lifecycle | `revokeAbility` rejects an active grant; re-grant changes native identity and event matching |
| Definition replacement | `replaceTimelineAbility` swaps future definitions while preserving existing execution snapshots |
| Effect lifecycle | `eventCount` / `eventAt` drive modifier application, refresh and cleanup; `remove` explicitly dispels |
| Effect queries | `find`, `taggedCount`, `taggedAt`, `effectCount`, `effectAt`, IDs, tags and payload reads drive gameplay and inspection |
| Modifier inspection | `getModifierCount`, `getModifierSource`, `getModifierOperation` and `getModifierAttribute` expose native contributions |
| Damage | Native `DamageRuntime` commits hits; target HP is read directly from CombatRuntime |
| Feedback | Articulated robot props, sword motion, impact sparks and a short runtime-event log |

The sample's game adapter checks energy and debits the AttributeSet after native
activation admission. It also projects effect lifecycle into the attribute
modifier and uses the final Attack value for damage. These are explicit script
composition responsibilities: this example does not bind the native transactional
`CostSpec` resource provider or claim UE5 GAS network prediction support.
No gameplay cooldown, HP or effect-duration countdown is duplicated in the HUD.
Robot pose, spark lifetime and the event log are presentation-only state.

This sample uses procedural robot meshes rather than skeletal animation. For a
skinned character and Montage authoring, see `examples/combat-action-editor`.

## Verification

Deterministic native gameplay checks require no display or network. They compile
this sample's actual game adapter with the engine's EveScript compiler and use
native Action/Attribute/Effect/Combat providers; only keyboard polling and the
unbuilt render-object arrays are replaced in the fixture.

```bash
python3 examples/combat-arena/tests/run.py --engine build/linux-debug/src/engine/eve
```

The checks cover cancellation, exclusive admission, grant/revoke/re-grant,
definition snapshot retention, cooldown preservation, payload-driven modifiers,
refresh identity, effect stack limits, dispel and expiry on both subjects.

Windowed smoke:

```bash
MIN_RUN_SECONDS=2 RUN_SECONDS=12 bash scripts/smoke_examples.sh combat-arena
```

On headless Linux, activate the prepared environment and run with Xvfb, software
Vulkan and null audio as described in AGENTS.md. Capture visual evidence with the
engine MCP screenshot tool. Check the logged normal/empowered hits, cooldown
rejection without extra energy loss, and effect expiry restoring Attack to 12.
