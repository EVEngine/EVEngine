#pragma once

#include "common/Time.h"

/**
 * @file SkillAction.h
 * @brief RPG Skill to renderer-independent Action adapter.
 */

#include "action/Action.h"
#include "rpg/Skill.h"

#include <optional>

namespace eve::rpg {

/**
 * @brief Translates a SkillDefinition into the common gameplay action model.
 *
 * This adapter owns no cast timer, phase or target lifecycle. The caller owns
 * one ActionRuntime and may submit the produced values to it. RPG condition,
 * resource-account and effect providers are injected into that runtime.
 */
class SkillActionAdapter {
public:
    /**
     * @brief Build the common definition for one RPG skill.
     * @param skill Borrowed definition observed during this call only.
     * @return An action definition or a structured mapping failure.
     */
    [[nodiscard]] static eve::Result<action::ActionDefinition> makeDefinition(const SkillDefinition& skill);

    /**
     * @brief Build a request for one skill invocation.
     * @param skill Borrowed definition observed during this call only.
     * @param source Optional source ECS handle; no raw pointer is retained.
     * @param target Optional target ECS handle for non-self skills.
     * @param requestedTick Deterministic simulation tick of the request.
     * @return An owning action request or a structured mapping failure.
     */
    [[nodiscard]] static eve::Result<action::ActionRequest> makeRequest(
        const SkillDefinition& skill, std::optional<ecs::EntityHandle> source = std::nullopt,
        std::optional<ecs::EntityHandle> target        = std::nullopt,
        eve::SimulationTick              requestedTick = eve::SimulationTick::zero());

    /**
     * @brief Translate and submit one skill to an existing lifecycle owner.
     * @param runtime Caller-owned ActionRuntime; it remains the sole state owner.
     * @return New execution identity or a checked validation failure.
     */
    [[nodiscard]] static eve::Result<action::ActionExecutionId> submit(
        action::ActionRuntime& runtime, const SkillDefinition& skill,
        std::optional<ecs::EntityHandle> source = std::nullopt, std::optional<ecs::EntityHandle> target = std::nullopt,
        eve::SimulationTick requestedTick = eve::SimulationTick::zero());
};

}  // namespace eve::rpg
