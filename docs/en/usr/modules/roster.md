# Roster: squad membership, post occupancy, reinforce requests

**Entry:** `eve.Roster()`, slot `roster`

Owns squad membership, post seats, vacancy events, and delayed spawn *requests*.
It does not own HP, navigation, production queues, or behavior trees. Games fulfill
`spawnRequest` values through Production / an `ISpawnProvider`, then call
`ackSpawnRequest` and `assignPost` / `addMember`.

See the Chinese module guide [`docs/usr/modules/roster.md`](../../modules/roster.md)
and `examples/post-patrol-reinforce` for the vertical cut.
