# Single source of truth for the aggregate third-party revision. CI cache keys
# hash this file, so moving the pin automatically selects a new binary cache.
set(EVENGINE_THIRD_PARTY_PIN "828bcb334d4e41bbecbf4fdac67c8d09f196c501" CACHE STRING
    "Pinned commit for the EVEngine/third-party aggregate checkout. The checkout is
     moved onto this commit at configure time (or verified against it), and every
     prebuilt third-party tree must be built from it.")
set(EVENGINE_THIRD_PARTY_RELEASE "v0.1.2" CACHE STRING
    "Immutable EVEngine/third-party release whose tag resolves to EVENGINE_THIRD_PARTY_PIN")
set(EVENGINE_THIRD_PARTY_MODE "AUTO" CACHE STRING
    "AUTO downloads releases with source fallback on unavailable downloads; SOURCE builds dependencies; PREBUILT requires a published package")
set_property(CACHE EVENGINE_THIRD_PARTY_MODE PROPERTY STRINGS AUTO PREBUILT SOURCE)
