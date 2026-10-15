# Protobuf persistence migration

Known persistent data should be described by module-owned `.proto` files and
serialized through generated protobuf messages. A domain owner remains the
authority for its live state; a protobuf message is an owning snapshot, not a
second mutable runtime model. `Value` remains available for genuinely dynamic
script and tool data, but is not the serialization step for known records.

## Implemented typed persistence: inventory and RPG state

`proto/inventory_save.proto` defines the version-1 inventory record. The
generated C++ sources in `proto/generated/` come from the pinned protobuf
35.1 `protoc`; they are checked in so cross-compilation does not require a
host compiler for the target platform. `InventorySaveSession` writes binary
protobuf and restores it through a private prepared state. Its existing JSON
API is a compatibility projection through protobuf's JSON mapping, using the
same message and domain validation. Neither route uses `Value` or hand-written
JSON field encoding in the inventory session.

The wire record carries `schema` and `version`. Version 1 rejects unknown
binary fields recursively, unknown JSON fields, unsupported versions,
unregistered policies and item definitions, duplicate identities, and invalid
equipment placement. Restore publishes the bag and equipment together only
after all checks pass. A later version needs an explicit migration from version
1; changing the field numbers or reusing removed numbers is forbidden.

The inventory module links the full protobuf runtime only when it is enabled.
The engine CMake graph builds that target directly from the pinned `third-party`
checkout so the imported protobuf target carries its Abseil link dependencies
on the first configure. The generated files can be refreshed with the pinned
Protobuf 35.1 compiler:

```sh
python3 scripts/generate_protobuf_cpp.py --protoc /path/to/pinned/protoc
```

The script passes each link group's export macro through protoc's
`dllexport_decl` option and inserts `common/Export.h` into generated headers.
This lets Windows SHARED consumers link message classes and descriptors from
their owning engine DLL. Verify the generated diff before committing.
Runtime descriptor pools and dynamic messages
can consume `FileDescriptorSet` produced from dynamically authored `.proto`
files:

```sh
protoc --proto_path=proto --include_imports \
  --descriptor_set_out=build/inventory.desc proto/inventory_save.proto
```

`schema::ProtobufSchema::registerDescriptorSet` loads these descriptor bytes
at runtime, and its `encodeJson`/`decodeJson` methods use dynamic messages.
Compiling a newly authored `.proto` into a descriptor set remains a host-tool
step outside the cross-compiled runtime.

## Remaining migration boundaries

`Tracker` quest progress now has a typed `TrackerSave` message and binary
snapshot API. The old JSON entry points use Protobuf JSON mapping for version-1
content. The quest registry remains the authoritative definition source;
restoration validates every quest/objective identity and bound in a private
candidate before publishing it. Runtime event queues are intentionally absent
from the persistent message. Version 1 retains the previous policy of ignoring
unknown fields while rejecting unsupported versions.

`GameState` now captures switches, numeric variables and scoped facts into a
typed `GameStateSave` message. New binary and JSON snapshots share the same
schema/version and finite-number checks. Restore builds private maps and swaps
them only after all fields validate. Version-1 JSON written before this change
used nested scope maps; its parser remains a read-only compatibility migration
through Protobuf `Struct` until existing saves have been upgraded. Neither new
snapshots nor this legacy read path pass through `eve::Value`.

`RPGActor` checkpoints use `ActorCheckpointSave` for binary and Protobuf JSON
output. Restore checks the destination's class, trait and attribute layout,
progression bounds, vitals and registered skills before publishing any state.
The actor's active effects and casts remain excluded and block capture. Existing
version-1 JSON field names map directly onto the protobuf message.

`Party` checkpoints now embed those actor messages directly in an ordered
`PartyCheckpointSave`. Binary and JSON restore validate every member ID, actor
candidate and roster length before changing any actor; the party's ECS links
remain owned by the live roster.

`RPGSaveSession` now writes a version-3 Protobuf envelope. Its payload contains
typed RPG messages and an inventory `Any` carrying the inventory module's own
wire record. The envelope hashes the exact stored payload bytes so integrity
verification does not depend on re-serializing a parsed Protobuf message.
Content-version migrations rewrite private typed messages before participant
preparation. Version-1/2 common JSON envelopes remain read-only compatibility
inputs and still use the old envelope parser; new binary and JSON writes do
not use `Value`.

`Value` remains a supported runtime type for genuinely dynamic script, editor
and tool data. The migration target is its use as an intermediate format for
known persistence records, not the `Value` type itself.

The generic `GameSaveSession` now writes a version-2 Protobuf envelope with
typed `Any` participant records, a content version, and a digest over the exact
payload wire bytes. It prepares every participant before publishing any state;
version-1 JSON is a read-only compatibility input. The RPG adapter uses its
typed binary envelope directly. Tactics still wraps its old JSON snapshot in a
Protobuf `BytesValue` participant, so its own persistence codec must migrate to
typed `.proto` messages before this boundary is complete.

Other domain snapshots, authored assets, event logs, and network records need
their own `.proto` definitions, fixture migrations, and domain validation. The
custom `SchemaRegistry` can be retired from persistent structural validation
only after its consumers move to protobuf descriptors. Constraints such as
registered IDs, numeric ranges, and cross-field relationships remain explicit
domain checks; descriptor structure alone does not enforce them. Each migration
must read its old format, reject unsupported future versions, and prove that a
failed load leaves the authoritative state unchanged.
