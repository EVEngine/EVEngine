# Single source of truth for the aggregate third-party revision. CI cache keys
# hash this file, so moving the pin automatically selects a new binary cache.
set(EVENGINE_THIRD_PARTY_PIN "477333b3e4045a2c09a9ef8cf761acb94e9eb6f6" CACHE STRING
    "Pinned commit for the EVEngine/third-party aggregate checkout. The checkout is
     moved onto this commit at configure time (or verified against it), and every
     prebuilt third-party tree must be built from it.")
set(EVENGINE_THIRD_PARTY_RELEASE "v0.1.0" CACHE STRING
    "Immutable EVEngine/third-party release whose tag resolves to EVENGINE_THIRD_PARTY_PIN")
set(EVENGINE_THIRD_PARTY_MODE "AUTO" CACHE STRING
    "AUTO downloads releases with source fallback on unavailable downloads; SOURCE builds dependencies; PREBUILT requires a published package")
set_property(CACHE EVENGINE_THIRD_PARTY_MODE PROPERTY STRINGS AUTO PREBUILT SOURCE)
