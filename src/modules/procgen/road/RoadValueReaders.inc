// Shared private scalar readers.
const RoadCodecValue* field(const RoadCodecValue& value, const char* name) {
    const auto* object = value.getIf<RoadCodecValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

bool readId(const RoadCodecValue& value, const char* name, std::uint32_t& output) {
    const auto* item = field(value, name);
    const auto* id   = item ? item->getIf<std::int64_t>() : nullptr;
    if (!id || *id <= 0 || *id > std::numeric_limits<std::uint32_t>::max()) return false;
    output = static_cast<std::uint32_t>(*id);
    return true;
}

bool readNumber(const RoadCodecValue& value, const char* name, float& output) {
    const auto* item   = field(value, name);
    const auto* number = item ? item->getIf<double>() : nullptr;
    if (!number || !std::isfinite(*number) || *number < -std::numeric_limits<float>::max() ||
        *number > std::numeric_limits<float>::max())
        return false;
    output = static_cast<float>(*number);
    return true;
}

bool readOptionalNumber(const RoadCodecValue& value, const char* name, float& output) {
    return field(value, name) == nullptr || readNumber(value, name, output);
}

bool readOptionalBool(const RoadCodecValue& value, const char* name, bool& output) {
    const auto* item = field(value, name);
    if (!item) return true;
    const auto* boolean = item->getIf<bool>();
    if (!boolean) return false;
    output = *boolean;
    return true;
}
