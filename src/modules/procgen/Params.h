#pragma once

#include "common/Value.h"

#include <cstdint>
#include <string>

namespace eve::procgen {

/**
 * @brief Owning, typed generation parameters.
 *
 * Algorithm-specific values are stored in a common `eve::Value::Object`; the
 * setter overload used by the caller is therefore preserved as the Value kind
 * instead of being serialized to text and parsed again by a getter.
 *
 * `seed`, `width`, and `height` have a dedicated generation-dimension domain.
 * `setSeed`/`setSize` are the authoritative APIs for that domain and
 * `setInt` retains the established convenience spelling for those three keys.
 * A floating/string/bool value with one of those names is an algorithm-local
 * parameter and is kept in the typed object domain; it does not change the
 * dimensions. This is required by existing mesh recipes which use `width` or
 * `height` as geometric floating-point parameters.
 */
class Params {
public:
    /** @brief Set the deterministic seed used by generation. */
    void     setSeed(uint32_t seed);
    /** @brief Return the deterministic generation seed. */
    uint32_t getSeed() const;

    /**
     * @brief Set the generation dimensions, clamping each non-positive value to one.
     * @param width Number of generated columns.
     * @param height Number of generated rows.
     */
    void setSize(int width, int height);
    /** @brief Return the generation width. */
    int  getWidth() const;
    /** @brief Return the generation height. */
    int  getHeight() const;

    /**
     * @brief Store an Int64 parameter, preserving its numeric Value kind.
     * @param key Algorithm parameter key; the three dimension keys route to
     *        the dedicated dimension domain.
     * @param value Integer value.
     */
    void setInt(const std::string &key, int value);
    /** @brief Store a finite or non-finite floating parameter as Value::Double without text conversion. */
    void setFloat(const std::string &key, float value);
    /**
     * @brief Stores a boolean generation parameter.
     * @param key Parameter name.
     * @param value Boolean value to store.
     */
    void setBool(const std::string &key, bool value);
    /** @brief Store an owning string parameter as Value::String. */
    void setString(const std::string &key, const std::string &value);
    /** @brief Return whether a parameter or one of the three dimensions exists. */
    bool has(const std::string &key) const;

    /**
     * @brief Read an integer parameter with explicit numeric conversion rules.
     *
     * Int64 values must fit `int`; finite integral Double values must also fit;
     * Bool values convert to zero or one. Strings, fractional values,
     * out-of-range values, missing keys, and unsupported kinds return the
     * supplied default. Text is never parsed.
     */
    int getInt(const std::string &key, int defaultValue) const;
    /**
     * @brief Read a floating parameter with explicit numeric conversion rules.
     *
     * Double and representable Int64 values convert to float; Bool converts to
     * zero or one. Non-finite/out-of-range values and unsupported kinds return
     * the supplied default. Strings are never parsed.
     */
    float getFloat(const std::string &key, float defaultValue) const;
    /**
     * @brief Read a boolean parameter without parsing text.
     * @param key Parameter name.
     * @param defaultValue Value returned when the parameter is absent, invalid,
     *        or not an exact Bool/0/1 numeric value.
     * @return The converted boolean value or @p defaultValue.
     */
    bool getBool(const std::string &key, bool defaultValue) const;
    /** @brief Read only a Value::String parameter; numeric/bool values are not stringified. */
    std::string getString(const std::string &key, const std::string &defaultValue) const;

    /**
     * @brief Return a deterministic serialization of all generation parameters.
     * @return Sorted, length-delimited, type-tagged parameter text suitable for a build key.
     * @remarks The result contains the dedicated dimensions and typed algorithm
     *          values. It does not include wall-clock, process address or
     *          unordered-container iteration order; a string `"1"`, Int64 `1`,
     *          Double `1.0`, and Bool `true` have different encodings.
     */
    [[nodiscard]] std::string canonicalString() const;

private:
    uint32_t        seed_   = 1;
    int             width_  = 32;
    int             height_ = 32;
    eve::Value::Object values_;
};

}  // namespace eve::procgen
