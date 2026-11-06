#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace eve::roster {

/** @brief One open post/squad seat after a subject left. */
struct Vacancy {
    std::string   kind;         ///< "post" or "squad"
    std::string   targetId;     ///< Post or squad logical id
    std::string   role;         ///< Required role for refill
    std::string   lostSubject;  ///< Subject that vacated
    std::uint64_t tick = 0;     ///< Simulation tick when recorded
};

/** @brief One pending spawn the game must fulfill (or fail). */
struct SpawnRequest {
    std::string   requestId;
    std::string   policyId;
    std::string   preferredDef;  ///< Definition / archetype id to spawn
    std::string   postId;
    std::string   squadId;
    std::string   role;
    std::uint64_t readyAtTick = 0;
};

/**
 * @brief Authoritative squad membership, post occupancy, and reinforce spawn requests.
 *
 * Owns only roster facts (who belongs where, which seats are empty, which spawn
 * requests are pending). Does not own HP, navigation, production queues, or NPC
 * behavior trees — games inject subject ids and fulfill spawn requests via their
 * own Production / ISpawnProvider path.
 *
 * @thread Simulation owner thread only.
 * @reentrancy No callbacks; no graphics includes.
 */
class EVENGINE_API_FOUNDATION Roster : public Module {
public:
    Module_REG(Roster);
    /** @brief Constructs an empty roster world. */
    Roster();
    /** @brief Destroys roster storage. */
    ~Roster() override;

    /** @brief Clears every post, squad, policy, vacancy and spawn request. */
    void clear();

    /**
     * @brief Registers a named post with a fixed seat capacity.
     * @param postId Stable logical id (non-empty, unique among posts).
     * @param capacity Maximum simultaneous occupants (>=1).
     * @param defaultRole Role written on assign when caller passes an empty role.
     * @return Applied, AlreadyExists, or InvalidArgument.
     */
    [[nodiscard]] Result<void> registerPost(std::string postId, int capacity, std::string defaultRole = "guard");

    /** @brief Removes a post and emits vacancies for every remaining occupant. */
    [[nodiscard]] Result<void> removePost(std::string_view postId, std::uint64_t tick);

    /**
     * @brief Occupies one post seat with a subject.
     * @remarks Rejects when the post is full, the subject already holds any post seat,
     *          or ids are empty. Role defaults to the post's defaultRole when empty.
     */
    [[nodiscard]] Result<void> assignPost(std::string_view postId, std::string subject, std::string role = {});

    /** @brief Releases one subject's seat on a post and records a vacancy. */
    [[nodiscard]] Result<void> releasePost(std::string_view postId, std::string_view subject, std::uint64_t tick);

    /** @brief Current occupants for a post, or NotFound. */
    [[nodiscard]] Result<int> postOccupancy(std::string_view postId) const;
    /** @brief Configured capacity for a post, or NotFound. */
    [[nodiscard]] Result<int> postCapacity(std::string_view postId) const;
    /** @brief Assignee at a stable index (sorted by subject id), or empty / failure. */
    [[nodiscard]] Result<std::string> postAssigneeAt(std::string_view postId, int index) const;
    /** @brief Role for an assignee on a post, or NotFound. */
    [[nodiscard]] Result<std::string> postRole(std::string_view postId, std::string_view subject) const;

    /**
     * @brief Creates a squad with a membership cap and optional initial intent.
     * @param intent Shared collective intent id (e.g. "patrol", "hold"); may be empty.
     */
    [[nodiscard]] Result<void> createSquad(std::string squadId, int capacity, std::string intent = {});

    /** @brief Destroys a squad and emits vacancies for every remaining member. */
    [[nodiscard]] Result<void> destroySquad(std::string_view squadId, std::uint64_t tick);

    /** @brief Adds a member with a role; rejects duplicates and capacity overflow. */
    [[nodiscard]] Result<void> addMember(std::string_view squadId, std::string subject, std::string role = "member");

    /** @brief Removes a member and records a squad vacancy. */
    [[nodiscard]] Result<void> removeMember(std::string_view squadId, std::string_view subject, std::uint64_t tick);

    /** @brief Replaces the squad's shared intent string. */
    [[nodiscard]] Result<void> setIntent(std::string_view squadId, std::string intent);
    /** @brief Reads the squad intent, or NotFound. */
    [[nodiscard]] Result<std::string> intent(std::string_view squadId) const;
    /** @brief Current member count, or NotFound. */
    [[nodiscard]] Result<int> memberCount(std::string_view squadId) const;
    /** @brief Member subject at a stable index, or failure. */
    [[nodiscard]] Result<std::string> memberAt(std::string_view squadId, int index) const;
    /** @brief Role for a squad member, or NotFound. */
    [[nodiscard]] Result<std::string> memberRole(std::string_view squadId, std::string_view subject) const;

    /**
     * @brief Removes a subject from every post and squad, emitting one vacancy per seat.
     * @return Number of seats released.
     */
    [[nodiscard]] Result<int> releaseSubject(std::string_view subject, std::uint64_t tick);

    /** @brief Queued vacancy count (stable insertion order). */
    [[nodiscard]] int vacancyCount() const;
    /** @brief Vacancy at index, or InvalidArgument when out of range. */
    [[nodiscard]] Result<Vacancy> vacancyAt(int index) const;
    /** @brief Clears the vacancy queue without changing occupancy. */
    void clearVacancies();

    /**
     * @brief Upserts a reinforce policy that turns matching vacancies into spawn requests.
     * @param policyId Stable id.
     * @param preferredDef Definition/archetype the game should spawn first.
     * @param delayTicks Ticks to wait after a vacancy before enqueueing a request (>=0).
     * @param budget Maximum successful acks this policy may produce (0 = unlimited).
     * @param postId When non-empty, only vacancies for this post match.
     * @param squadId When non-empty, only vacancies for this squad match.
     * @param role When non-empty, only vacancies with this role match.
     */
    [[nodiscard]] Result<void> setPolicy(std::string policyId, std::string preferredDef, int delayTicks, int budget,
                                         std::string postId = {}, std::string squadId = {}, std::string role = {});

    /** @brief Adds or replaces a preferred→fallback definition edge for a policy. */
    [[nodiscard]] Result<void> setFallback(std::string_view policyId, std::string preferredDef,
                                           std::string fallbackDef);

    /** @brief Removes a policy; pending requests for it remain until acked/failed. */
    [[nodiscard]] Result<void> clearPolicy(std::string_view policyId);

    /**
     * @brief Advances reinforce matching for vacancies ready at or before @p tick.
     * @return Number of newly enqueued spawn requests.
     * @remarks Does not invent subjects. The game must ack or fail each request.
     * @cost Linear in vacancy×policy counts; amortize once per simulation tick.
     */
    [[nodiscard]] Result<int> step(std::uint64_t tick);

    /** @brief Pending spawn request count. */
    [[nodiscard]] int spawnRequestCount() const;
    /** @brief Spawn request at index, or InvalidArgument. */
    [[nodiscard]] Result<SpawnRequest> spawnRequestAt(int index) const;

    /**
     * @brief Marks a spawn request fulfilled and consumes one budget unit when limited.
     * @remarks Does not assign the new subject; the game calls assignPost/addMember after spawn.
     */
    [[nodiscard]] Result<void> ackSpawnRequest(std::string_view requestId);

    /**
     * @brief Fails a spawn request; enqueues a fallback definition when configured, else drops it.
     */
    [[nodiscard]] Result<void> failSpawnRequest(std::string_view requestId, std::uint64_t tick);

private:
    struct Occupant {
        std::string subject;
        std::string role;
    };
    struct Post {
        int                   capacity = 0;
        std::string           defaultRole;
        std::vector<Occupant> occupants;
    };
    struct Squad {
        int                   capacity = 0;
        std::string           intent;
        std::vector<Occupant> members;
    };
    struct Policy {
        std::string                        preferredDef;
        int                                delayTicks = 0;
        int                                budget     = 0;  ///< 0 = unlimited
        int                                spent      = 0;
        std::string                        postId;
        std::string                        squadId;
        std::string                        role;
        std::map<std::string, std::string> fallbacks;  ///< preferred → fallback
    };

    [[nodiscard]] Result<void> fail(DiagnosticCode code, std::string message, std::string path = {}) const;
    template <class T>
    [[nodiscard]] Result<T> failT(DiagnosticCode code, std::string message, std::string path = {}) const;
    void               pushVacancy(std::string kind, std::string targetId, std::string role, std::string lostSubject,
                                   std::uint64_t tick);
    [[nodiscard]] bool policyMatches(const Policy& policy, const Vacancy& vacancy) const;
    [[nodiscard]] bool budgetAvailable(const Policy& policy) const;
    [[nodiscard]] std::string  nextRequestId();
    [[nodiscard]] Result<void> eraseSpawnRequest(std::string_view requestId);

    std::map<std::string, Post>   posts_;
    std::map<std::string, Squad>  squads_;
    std::map<std::string, Policy> policies_;
    std::vector<Vacancy>          vacancies_;
    std::vector<SpawnRequest>     spawnRequests_;
    std::uint64_t                 nextRequestSerial_ = 1;
};

}  // namespace eve::roster
