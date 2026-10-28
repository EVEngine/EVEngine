# Canonical terrain material schema v3

Schema v3 adds stable optional image asset references to the source identities retained by
`eve.terrain-material/2`. Every layer owns `diffuseAsset`, `normalAsset`, `weightAsset`, and
`maskAsset`; the root owns `holesAsset` and exactly four `controlAssets`. A non-empty value must
be a canonical `asset://<uuid>` reference. Empty values are explicit missing bindings and allow
source-only or legacy terrain definitions to retain metadata without inventing assets.

Unity import derives each reference from the collection package identity, normalized source GUID,
and the canonical `image:default` role. Original `unity-guid:` strings remain for diagnostics and
round-trip provenance. Runtime loading parses references into strong `AssetRef` values before
publishing the detached terrain material.

The v2-to-v3 migration writes empty references because an old definition does not contain enough
information to recover its package-relative image identities. It preserves source strings and all
vendor fields. Definitions with unversioned v3 members are rejected. The compatibility window is
v2 only.

`asset::decodeEvpackImage` is the backend-neutral, bounded EVIMG decoder used by both terrain atlas
construction and `EvpackImageLoader`. It owns validated RGBA8 mip bytes and performs no GPU calls.
This keeps header, definition, color-transfer, mip-count and byte-budget checks in one place.

Runtime construction resolves those references into grouped CPU images, bakes normal convention
and mask remaps, then repacks up to sixteen layers for a single draw. The packed contract contains
4x4 material atlases, four control maps plus holes, and lossless per-layer parameters. Upload and
release use the graphics resource interface transactionally; the runtime object retains no reader
or archive pointers. See `procgen-terrain-material-shader.md`.
