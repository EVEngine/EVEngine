# Single source of truth for the aggregate third-party revision. CI cache keys
# hash this file, so moving the pin automatically selects a new binary cache.
set(EVENGINE_THIRD_PARTY_PIN "7e5f020cba1baf342ee1c004644b6637e6f5f9e9" CACHE STRING
    "Pinned commit for the EVEngine/third-party aggregate checkout. The checkout is
     moved onto this commit at configure time (or verified against it), and every
     prebuilt third-party tree must be built from it.")
set(EVENGINE_THIRD_PARTY_RELEASE "v0.1.0" CACHE STRING
    "Last published binary dependency release; source mode uses EVENGINE_THIRD_PARTY_PIN")
# Protobuf and Abseil landed after v0.1.0. The published binary package is
# pinned to an older aggregate commit, so source mode is the compatible default
# until a release for EVENGINE_THIRD_PARTY_PIN is published.
set(EVENGINE_THIRD_PARTY_MODE "SOURCE" CACHE STRING
    "AUTO downloads releases with source fallback on unavailable downloads; SOURCE builds dependencies; PREBUILT requires a published package")
set_property(CACHE EVENGINE_THIRD_PARTY_MODE PROPERTY STRINGS AUTO PREBUILT SOURCE)
