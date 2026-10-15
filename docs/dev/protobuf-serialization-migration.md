# Protobuf persistence migration

Known persistent data should be described by module-owned `.proto` files and
serialized through generated protobuf messages. A domain owner remains the
authority for its live state; a protobuf message is an owning snapshot, not a
second mutable runtime model. `Value` remains available for genuinely dynamic
script and tool data, but is not the serialization step for known records.

## Implemented typed persistence: inventory and quest tracker

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
on the first configure. The generated files can be refreshed with:

```sh
protoc --proto_path=proto --cpp_out=proto/generated proto/inventory_save.proto
```

Use the `protoc` version pinned in `third-party/protobuf` and verify the
generated diff before committing. Runtime descriptor pools and dynamic messages
can consume `FileDescriptorSet` produced from dynamically authored `.proto`
files:

```sh
protoc --proto_path=proto --include_imports \
  --descriptor_set_out=build/inventory.desc proto/inventory_save.proto
```

The engine-side dynamic descriptor loader is not implemented in this first
slice; the host tool that compiles those files is a separate concern from the
cross-compiled runtime.

## Remaining migration boundaries

`Tracker` quest progress now has a typed `TrackerSave` message and binary
snapshot API. The old JSON entry points use Protobuf JSON mapping for version-1
content. The quest registry remains the authoritative definition source;
restoration validates every quest/objective identity and bound in a private
candidate before publishing it. Runtime event queues are intentionally absent
from the persistent message. Version 1 retains the previous policy of ignoring
unknown fields while rejecting unsupported versions.

`Value` remains a supported runtime type for genuinely dynamic script, editor
and tool data. The migration target is its use as an intermediate format for
known persistence records, not the `Value` type itself.

`GameSaveSession` and its RPG/Tactics participants still exchange `Value`
payloads and JSON. Their next contract should be typed participant messages
inside a versioned game-save envelope, with an explicit participant type name
and version. The current canonical JSON hash cannot simply become a hash of
re-serialized protobuf bytes: protobuf does not promise canonical wire output.
Hash either the original immutable wire bytes or a separately specified
canonical digest representation, and retain verification before any participant
commit.

Other domain snapshots, authored assets, event logs, and network records need
their own `.proto` definitions, fixture migrations, and domain validation. The
custom `SchemaRegistry` can be retired from persistent structural validation
only after its consumers move to protobuf descriptors. Constraints such as
registered IDs, numeric ranges, and cross-field relationships remain explicit
domain checks; descriptor structure alone does not enforce them. Each migration
must read its old format, reject unsupported future versions, and prove that a
failed load leaves the authoritative state unchanged.
