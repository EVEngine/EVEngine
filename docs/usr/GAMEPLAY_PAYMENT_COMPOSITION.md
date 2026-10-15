# Composing card, economy and RTS payments

EconomyLedger owns balances and accounting counters. Keep one persistent
EconomyLedgerResourceAccount (or RTSEconomyAdapter) per ledger while reservations
are in flight; do not wrap the same ledger with independent competing adapters.
Card and RTS adapters borrow this account for synchronous owner-thread calls.

## Script composition

Each domain contributes its own operation to the existing transaction module:

```squirrel
local batch = transaction.newAtomicBatch();
cards.stagePlay(batch, card);
sim.stageAbility(batch, casterId, "grenade", "", x, y);
local result = batch.commit();
if (!result.ok) message = result.status.summary;
```

Staging does not pay, consume a card, start a cooldown or publish a production
task. A failed staging Result poisons the batch, even when ignored. Commit adds
all costs and uses one reservation plus the existing Coordinator. It consumes
the builder on success or failure. Use
`sim.stageProduction(batch, producerId, newUnitId, "unit:marine")` to compose
normal production instead. Settlement and cancellation use the existing queues;
cancellation refunds the unit price, while the consumed card activation remains
spent. Resource floors apply to the complete merged payment.

Cards also compose with Economy without creating an RTS instance:

```squirrel
local batch = transaction.newAtomicBatch();
cards.stagePlay(batch, card);
economy.stagePayment(batch, playerId, "{\"ore\":2}");
local result = batch.commit();
```

`sim.stagePayment(batch, factionId, costJson)` contributes an extra cost to an
RTS faction's account. Cost objects contain resource names and positive integers.
Card Mana, ability, production and additional costs are added, not overwritten.
A batch has one authoritative payer; different payer contributions fail explicitly.

Create and commit an RTS batch before advancing simulation. Tick changes and
stale entity/account handles reject execution before accessing the account.
Script facades are retained by VM-visible roots until commit/disposal; native
callers retain borrowed stores themselves. Discarding an uncommitted batch makes
no gameplay changes. Pending batches are safe during VM shutdown. This transient
builder has no persistent/wire format, uses the simulation/VM owner thread,
rejects reentry and runs no arbitrary script callbacks inside the transaction.

Ability admission schedules the canonical cast; effects settle in the next fixed
step and respect cast/channel timing. Later interruption does not refund an
already committed card/cost. Card and RTS have no dependency on each other;
the batch owns participants, not balances, health or queues.

## Ruleset-specific card costs

The ordinary Card::play overload continues to charge CardDefinition::cost in
mana. A ruleset can supply CardPlayComposition::paymentCost to replace that
payment with a validated CostSpec containing any number of resources. This is
a per-play input, not a second mutable definition store. Compute discounts or
alternative currencies in the game rules before submitting the play.

```cpp
auto cost = eve::resource::CostSpec::from({{"ore", 3}, {"supply", 2}});
if (!cost) return eve::Result<void>::failure(cost.status());
eve::card::CardPlayComposition composition;
composition.paymentCost = std::move(cost).takeValue();
composition.effect = &spawnEffect;
composition.additionalEffects = {&researchEffect};
auto played = cards.play(card, economy.account(), std::move(composition));
if (!played) return eve::Result<void>::failure(played.status());
```

An omitted override uses mana. An engaged invalid or empty CostSpec is rejected;
it does not mean free. Free cards use definition cost zero with no override.
The condition tree still sees the definition's mana cost: custom affordability
UI should query account.canAfford with the supplied CostSpec. Admission checks
the actual payment again through the account reservation protocol.

## Multiple effects

Card commits container transfer, card state, the optional effect, additional
effects in listed order, and finally the resource debit. RTS supports
RTSEconomyAdapter::pay(cost, effects) with a span of participants in commit
order. All participants must be non-null and unique; duplicate or null entries
are rejected before prepare. Effect objects remain caller-owned and must live
until the call returns. Do not reenter the same card, containers or account
from participant callbacks.

Each participant must stage without publishing in prepare, discard staging in
rollback, and undo a successful commit in compensate. On failure the shared
Coordinator rolls back prepared participants and compensates committed ones
in reverse order. A participant that cannot compensate reports a structured
failure; atomic recovery depends on that participant's contract.

For a reinforcement card that queues RTS production, use RTSBuildRequest with
the building's canonical WorkQueue and CommandQueue, the shared ActionRuntime,
and the faction account. Set the request's one canonical multi-resource cost.
Evaluate the card condition through Card::evaluatePlay, then add a synchronous
CardPlayParticipant to RTSBuildRequest::additionalEffects alongside other
ruleset effects. RTSProductionActionAdapter::build publishes the order,
production task, card consumption, ruleset effects and payment in one
transaction. A later effect failure compensates card state and both queues.
Do not call Card::play and build separately for this workflow: those are two
independent transactions. The executable production test covers the shared
admission boundary; actual unit spawning remains the RTS settlement system's
responsibility once production finishes.

The checked economy account rejects balance, capacity and cumulative
income/expense overflow before publication. A failed multi-resource operation
does not publish a candidate ledger or charge only a subset of the resources.
The old direct EconomyLedger credit/debit APIs are compatibility operations;
use the resource account for checked transactional gameplay payments.

These additions introduce no ECS system, persistent format, capability lookup
or provider registry. Provider-free cards with cost zero still use the shared
transaction route. No independent link-trimming claim is made.

Executable composition and failure examples live in
test/card_payment_composition.cpp.
