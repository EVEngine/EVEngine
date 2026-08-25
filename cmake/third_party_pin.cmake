# Single source of truth for the aggregate third-party revision. CI cache keys
# hash this file, so moving the pin automatically selects a new binary cache.
set(EVENGINE_THIRD_PARTY_PIN "566e4073d2ec1c723b581daa50334a4b05956091" CACHE STRING
    "Pinned commit for the EVEngine/third-party aggregate checkout. The checkout is
     moved onto this commit at configure time (or verified against it), and every
     prebuilt third-party tree must be built from it.")
