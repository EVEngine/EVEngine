# Single source of truth for the aggregate third-party revision. CI cache keys
# hash this file, so moving the pin automatically selects a new binary cache.
set(EVENGINE_THIRD_PARTY_PIN "33c62497218a65e2b28fcc352a27d558733a6d88" CACHE STRING
    "Pinned commit for the EVEngine/third-party aggregate checkout. The checkout is
     moved onto this commit at configure time (or verified against it), and every
     prebuilt third-party tree must be built from it.")
