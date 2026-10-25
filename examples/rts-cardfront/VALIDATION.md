# Cardfront validation

Validated on native Windows/Vulkan on 2026-10-13 with a freshly built engine and
standard load.nut startup. The scene independently stages Card and RTS operations
through Transaction.newAtomicBatch; no pair-specific module remains.

All 55 Card cases passed, including the complete scenario collecting, building,
producing, using skills, winning and restarting. Generic contract cases cover
Card plus Economy without RTS, merged resource floors, later-participant failure
compensation, poisoned failed staging, stale entity/account/tick rejection,
retained script owners and pending batch disposal at VM shutdown. The existing
61 RTS economy, production, script and crowd cases also passed after production
was factored into reusable stages.

Both SHARED and OBJECT hosts linked. The OBJECT trim configuration uses
EVENGINE_PROFILE=minimal, EVENGINE_MODULE_card=ON, EVENGINE_MODULE_rts=OFF,
EVENGINE_MODULE_devtools=ON and EVENGINE_MODULE_font=ON. An actual engine run
asserted that the RTS class was absent, consumed a card and paid Mana plus ore
from Economy, printed GENERIC_CARD_ECONOMY_WITHOUT_RTS_PASS and presented frames
without runtime errors. This proves L1 compile, L2 link and L3 runtime composition
of Card plus Economy with RTS absent. Font and devtools are explicit host
providers; this does not claim that every engine dependency can be removed.

The official eve_play player actions completed the full scene with gathered=804,
built=true, produced=3, skillPlays=4, enemyHealth=0, outcome=victory, tick=7683.
All four cards were used through independent domain stages. The 1280x800 images
in captures/runtime.png and captures/victory.png are fresh engine readbacks from
this generic version; no desktop capture was used. Runtime logs contained no
script/frame errors. Restart restored 20 minerals, 16 Mana, no barracks, no army
and no played cards. The standard smoke harness passed its six-second window.

make check passed against origin/dev, including architecture contracts, module
layering, binding coverage, formatting and all 251 repository script tests.

The applicable rules are canonical ownership, structured Result, generation-safe
handles, shared transaction lifecycle, injected time, explicit module boundaries
and real trimmed-build evidence. No exception is used.
