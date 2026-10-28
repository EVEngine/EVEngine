# UE-only source boundary

Epic's **Game Animation Sample** is deliberately not stored or imported here.
Its Fab listing marks it as "UE-Only Content - Licensed for Use Only with
Unreal Engine-based Products". EVEngine is not an Unreal Engine-based product.

The repository's `tools/unreal-uasset-converter` delegates `.uasset` loading to
the user's Unreal Editor and exports eligible skeletal assets through Epic's
glTF Exporter. It requires an explicit confirmation that each source asset may
be converted for use outside Unreal Engine. The converter does not change the
source license, so the Game Animation Sample is not used by this demo.

This demo therefore uses the CC0 KayKit mannequin and animation libraries in
`../kaykit/`. The runtime composition is the same intended proof: imported
skeletal clips feed EVEngine Motion Matching, while the `climbing` module owns
geometry queries, candidate selection and constrained movement.
