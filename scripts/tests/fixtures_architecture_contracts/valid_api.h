/**
 * @brief Borrow a live item for the duration of this call.
 * @ownership borrowed
 * @lifetime call
 */
Item* borrowItem();

class Api {
public:
    /** @brief Test whether the item is ready. */
    bool isReady() const;
    bool getBool() const;

    // Natural state-query spellings are predicates, not operation outcomes.
    bool ok() const;
    bool passed() const;
    bool paused() const;
    bool active() const;
    bool ownsResource() const;
    bool usingGpu() const;
    bool usedBackendFallback() const;
    bool critical() const;
    bool disposed() const;
    bool changed() const;
    bool exact() const;
    bool asBool() const;
    bool initialized() const;
    bool activeExecuted() const;
    bool transactionStateEquals() const;
    bool contextMatches() const;

private:
    bool updateUnchecked(float dt);

    /** @brief Apply an operation and return its checked outcome. */
    [[nodiscard]] Result<void> apply();
};
