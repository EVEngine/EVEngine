#pragma once
/** @file RPGSpawnEditor.h
 * @brief map_rpg_spawn_editor module-interface: script composition of canonical RPG content and private spawn editing.
 */
#include "common/Export.h"
#include "common/Module.h"
namespace eve::map_rpg_spawn_editor {
/** @brief Own canonical RPG authoring content and one native map or template editor application.
 * @ownership Owns real registry/resolver/document/application owners. Window/UI/GPU services are synchronous borrows
 * in bound calls; only an SDL routing id and bounded owning event copies survive frames. Only explicit preview creates
 * actors, in its separate private Table.
 * @remarks Six faces: provides a bounded platform event observer; requires explicit Window/UI/Graphics/Filesystem
 * borrows and owning RPG definition services; private ECS/UIHost data; no domain event or MCP tools; script surface
 * is RPGSpawnEditor. Native input copies and routing ids are transient and are never serialized.
 * @cost Publish/open/startPreview perform full canonical content/map preparation per explicit builder configuration;
 * update processes at most 1024 queued samples per call; render scales with visible tiles, preview members and dirty
 * panel builds.
 * @lifetime Borrowed facades and GPU resources remain live through each bound call. The native window may retire
 * between calls; shutdown then verifies registry absence and cancels logical interaction before applying its policy.
 * @thread SDL/UI/script owner thread, outside Views. Mutating bound calls reject reentry; no locks across callbacks.
 * @remarks Required RPG skills must first be admitted through the real RPG content API; unit validation never
 * substitutes a missing skill. Call shutdown(window, policy) before native window teardown. policy is require_clean or
 * explicit discard. A matching window close/quit is intercepted while open, preserving drafts until the user resolves
 * the close panel.
 * @remarks openTemplate opens a template asset instead of a map, using the same canonical form, CAS save and runtime
 * JSON export. Asset identity is the explicit DocumentKey/URI; persisted canonical content supersedes initial source.
 * Template edits do not publish definitions or mutate map sessions; export/import is explicit. render accepts a null
 * target in template mode.
 * @remarks In map mode, startPreview(files, owner, seed, bodyRadius) freezes committed source and builds an isolated
 * canonical RPG/map session. stepPreview(nanoseconds) injects one nonnegative simulation duration; stopPreview observes
 * cleanup failures. Runtime content must remain stable while preview runs. update returns preview_action (and
 * preview_event for event input) for the outer script to execute explicit start/step/stop/event input outside UI
 * callbacks. Diagnostics are owning detached captures. Preview does not save assets, consume gameplay RNG or modify
 * formal actors; it does not run combat or AI. Pending transactions and structure drafts must be resolved before
 * preview start. Source edits require restart. triggerPreviewEvent(name) resolves frozen event predicates to qualified
 * handles, then accepts a zero-duration input step in this preview only. Unknown/empty event names do not advance time
 * or change activation. Restart/stop cancel pending preview requests; retained UI callbacks also verify
 * owner/epoch/incarnation before queuing inputs. applyPreviewPlayers(json) prepares a bounded owning World2D
 * player frame and accepts a zero-duration input step. [] clears players; ordinary steps/events preserve the last
 * accepted frame. Player text is a transient UI draft, not source configuration or a player actor. Invalid frame
 * input leaves simulation unchanged. Coordinates share the canonical region point reader; input uses the private
 * preview default WorkLimits budget. update returns preview_players when preview_action is players.
 * defeatPreviewUnit(subject) resolves a non-nil persistent subject in the current private membership, applies canonical
 * lethal HP damage, then submits a zero-duration step. A later step retries retained private events if advance fails;
 * this operation does not promise rollback of already committed domain damage. Unknown identities do not advance.
 * update returns preview_subject when preview_action is death; per-source buttons select one captured current member.
 * @remarks Tiled import is map-only. update returns import_action/path/guid/prototype for execution outside UI
 * callbacks. prepareTiledImport(files,path,sourceUuid,prototypeId) synchronously reads/admit geometry using the bound
 * pixel transform; optional prototypeId names an existing sidecar row for new objects. Empty permits existing
 * references only. Pending preview/gestures/drafts reject preparation. A proposal draws amber without source changes
 * and blocks normal close/startPreview until commitTiledImport or cancelTiledImport. Commit uses the existing form
 * history; stale/content failures retain the proposal. Explicit discard retires it. Template mode rejects import
 * operations.
 * @cost prepareTiledImport reads/parses the full file and canonical-compiles the candidate per explicit request;
 * commitTiledImport canonical-compiles one reversible source transaction. Cancel only releases unpublished facts.
 */
class EVENGINE_API_EDITORS RPGSpawnEditor final : public Module {
public:
    Module_REG(RPGSpawnEditor);
    /** @brief Allocate canonical content owners and register the bounded native event observer; no window is opened. */
    RPGSpawnEditor();
    /** @brief Withdraw the observer and release authoring owners; normal native capture cleanup requires shutdown. */
    ~RPGSpawnEditor() override;
    RPGSpawnEditor(const RPGSpawnEditor&)            = delete;
    RPGSpawnEditor& operator=(const RPGSpawnEditor&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rpg_spawn_editor
