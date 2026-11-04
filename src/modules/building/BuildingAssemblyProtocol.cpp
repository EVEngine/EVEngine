#include "building/BuildingAssemblyProtocol.h"
#include <cmath>
#include <limits>
#include "building/BuildingAssemblyInternal.h"
#include "building/PlacementWorld.h"
namespace eve::building {
using namespace assembly_detail;
namespace {
Result<void> checkWirePayload(const Value& value, size_t& remaining, size_t depth = 0) {
    if (remaining == 0 || (depth >= 64 && (value.isArray() || value.isObject())) ||
        (value.isDouble() && !std::isfinite(value.asDouble())))
        return fail<void>("House protocol response exceeds its finite 100000-value / 64-depth budget");
    --remaining;
    if (value.isArray()) {
        for (size_t i = 0; i < value.arraySize(); ++i) {
            auto result = checkWirePayload(value.at(i), remaining, depth + 1);
            if (!result.ok()) return result;
        }
    } else if (value.isObject()) {
        for (const auto& key : value.keys()) {
            auto result = checkWirePayload(*value.find(key), remaining, depth + 1);
            if (!result.ok()) return result;
        }
    }
    return Result<void>::success();
}
}  // namespace

Result<Value> executeHouseRequest(PlacementWorld& w, const Value& request) {
    const auto* op      = request.find("op");
    const auto* version = request.find("version");
    if (!op || !op->isString() || !version || !version->isInt64() || version->asInt() != 1)
        return fail<Value>("House request requires version 1 and operation");
    auto name = op->asString();
    if (name == "encode") {
        auto value = request.find("value");
        if (!value) return fail<Value>("Missing value to encode");
        auto json = value->toJson();
        if (!json.ok()) return Result<Value>::failure(json.status());
        return Result<Value>::success(std::move(json).takeValue());
    }
    if (name == "decode") {
        auto json = request.find("json");
        if (!json || !json->isString()) return fail<Value>("Missing JSON text");
        return Value::fromJson(json->asString());
    }
    if (name == "validateBlueprint") {
        auto value = request.find("blueprint");
        if (!value) return fail<Value>("Missing blueprint");
        auto blueprint = AssemblyService::decodeBlueprint(*value);
        if (!blueprint.ok()) return Result<Value>::failure(blueprint.status());
        return AssemblyService::encodeBlueprint(blueprint.value());
    }
    if (name == "snapshot") return AssemblyService::snapshotWorld(w);
    if (name == "restore" || name == "applyReceipt") {
        auto desired = request.find("state");
        if (!desired) return fail<Value>("Missing desired state");
        auto expected = request.find("expected");
        if (name == "applyReceipt" && !expected) return fail<Value>("Receipt needs expected state");
        auto result = expected ? AssemblyService::applyReceipt(w, *expected, *desired)
                               : AssemblyService::restoreWorld(w, *desired);
        if (!result.ok()) return Result<Value>::failure(result.status());
        return Result<Value>::success(Value::Object{});
    }
    if (name == "capture") {
        auto id = request.find("blueprintId"), ids = request.find("members"), origin = request.find("pose");
        if (!id || !id->isString() || !ids || !ids->isArray() || !origin)
            return fail<Value>("Capture needs blueprintId, members and pose");
        auto             guid = AssetGuid::parse(id->asString());
        auto             pose = parsePose(*origin);
        std::vector<int> members;
        const bool       poseOk = pose.ok();
        if (!guid || !poseOk) return fail<Value>("Invalid blueprint identity or capture pose");
        for (const auto& member : *ids->getIf<Value::Array>()) {
            if (!member.isInt64() || member.asInt() <= 0 || member.asInt() > std::numeric_limits<int>::max())
                return fail<Value>("Invalid member key");
            members.push_back(int(member.asInt()));
        }
        auto b = AssemblyService::capture(w, members, *guid, pose.value());
        if (!b.ok()) return Result<Value>::failure(b.status());
        return AssemblyService::encodeBlueprint(b.value());
    }
    auto id = request.find("assemblyId");
    if (!id || !id->isString()) return fail<Value>("Missing house identity");
    auto identity = AssemblyId::parse(id->asString());
    if (!identity) return fail<Value>("Invalid house identity");
    if (name == "members") {
        auto a = AssemblyService::assembly(w, *identity);
        if (!a.ok()) return Result<Value>::failure(a.status());
        Value::Array parts;
        for (const auto& [key, member] : a.value().members)
            parts.emplace_back(codec::instanceValue(codec::snapshot(w.buildings().at(member))));
        return Result<Value>::success(std::move(parts));
    }
    auto prepare = [&]() -> Result<PreparedAssembly> {
        if (name == "remove") return AssemblyService::prepareRemoval(w, *identity);
        if (name == "remodel") {
            auto members = request.find("members");
            if (!members || !members->isArray()) return fail<PreparedAssembly>("Missing remodel members");
            std::vector<PlacedBuilding> parts;
            for (const auto& value : *members->getIf<Value::Array>()) {
                // Canonical instance decoder requires a positive record id; new members use
                // explicit new=true and discard that temporary wire key before admission.
                auto p = codec::parseInstance(value);
                if (!p.ok()) return Result<PreparedAssembly>::failure(p.status());
                auto runtime = codec::runtime(p.value());
                if (auto add = value.find("new"); add && add->isBool() && add->asBool()) runtime.instanceId = 0;
                parts.push_back(std::move(runtime));
            }
            return AssemblyService::prepareRemodel(w, *identity, parts);
        }
        auto poseValue = request.find("pose");
        if (!poseValue) return fail<PreparedAssembly>("Missing target pose");
        auto pose = parsePose(*poseValue);
        if (!pose.ok()) return Result<PreparedAssembly>::failure(pose.status());
        if (name == "move") return AssemblyService::prepareMove(w, *identity, pose.value());
        if (name == "place" || name == "preview") {
            auto blueprint = request.find("blueprint");
            if (!blueprint) return fail<PreparedAssembly>("Missing blueprint");
            auto b = AssemblyService::decodeBlueprint(*blueprint);
            if (!b.ok()) return Result<PreparedAssembly>::failure(b.status());
            return AssemblyService::preparePlacement(w, b.value(), *identity, pose.value());
        }
        return fail<PreparedAssembly>("Unknown house operation", DiagnosticCode::Unsupported);
    };
    auto token = prepare();
    if (!token.ok()) return Result<Value>::failure(token.status());
    if (name == "preview") {
        Value::Array parts;
        for (const auto& p : token.value().preview()) parts.emplace_back(codec::instanceValue(codec::snapshot(p)));
        return Result<Value>::success(std::move(parts));
    }
    // Reserve the complete wire receipt before publishing the mutation. A script must
    // never receive ok/null after the native world has already changed.
    auto after = AssemblyService::snapshotWorld(*token.value().data_->candidate);
    if (!after.ok()) return Result<Value>::failure(after.status());
    size_t remaining    = 99998;  // receipt object and assemblyId
    auto   beforeBudget = checkWirePayload(token.value().data_->before, remaining, 1);
    if (!beforeBudget.ok()) return Result<Value>::failure(beforeBudget.status());
    auto afterBudget = checkWirePayload(after.value(), remaining, 1);
    if (!afterBudget.ok()) return Result<Value>::failure(afterBudget.status());
    auto committed = AssemblyService::commit(w, std::move(token).takeValue());
    if (!committed.ok()) return Result<Value>::failure(committed.status());
    auto receipt = std::move(committed).takeValue();
    return Result<Value>::success(Value::Object{{"assemblyId", receipt.assembly.format()},
                                                {"before", std::move(receipt.before)},
                                                {"after", std::move(receipt.after)}});
}
}  // namespace eve::building
