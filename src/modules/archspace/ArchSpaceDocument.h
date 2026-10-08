#pragma once
#include "common/Export.h"


#include "archspace/ArchSpaceTypes.h"
#include "common/Result.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::archspace {

/**
 * @brief Authoritative architectural space document (Pascal-like node graph).
 *
 * @ownership The document exclusively owns its nodes. Borrowed pointers returned by
 *            find() are invalidated by any mutating call.
 * @thread Owner thread only.
 * @failure Mutators return a checked Result and leave the previous document state unchanged.
 */
class EVENGINE_API_WORLD Document {
public:
    Document() = default;

    [[nodiscard]] const std::string&                           rootId() const noexcept { return rootId_; }
    [[nodiscard]] const std::unordered_map<std::string, Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::size_t                                  nodeCount() const noexcept { return nodes_.size(); }

    /**
     * @brief Borrow a node by id.
     * @ownership Non-owning pointer into this document's node map; do not free.
     * @lifetime Until the next mutating call.
     */
    [[nodiscard]] const Node* find(const std::string& id) const;
    /**
     * @brief Borrow a mutable node by id.
     * @ownership Non-owning pointer into this document's node map; do not free.
     * @lifetime Until the next structural mutation.
     */
    [[nodiscard]] Node* findMutable(const std::string& id);

    /**
     * @brief Insert a validated node and link it into the parent child list.
     * @return Applied on success; Rejected when id/kind/parent/geometry rules fail.
     */
    [[nodiscard]] eve::Result<void> insert(Node node);
    /**
     * @brief Replace authored fields while preserving id, kind, parent and children.
     * @return Applied on success; NotFound/Rejected when missing or invalid.
     */
    [[nodiscard]] eve::Result<void> replace(Node node);
    /**
     * @brief Delete a node and every descendant. Root deletion clears the document.
     * @return Applied on success; NotFound when the id is unknown.
     */
    [[nodiscard]] eve::Result<void> eraseCascade(const std::string& id);
    /** @brief Remove every node. */
    void clear();

    /**
     * @brief Create site → building → level skeleton when the document is empty.
     * @return Applied on success; Rejected when the document already has a root or ids collide.
     */
    [[nodiscard]] eve::Result<void> bootstrap(const std::string& siteId, const std::string& buildingId,
                                              const std::string& levelId, double levelHeight = 3.0);

    /**
     * @brief Create a closed room: perimeter walls + slab + zone under an existing level.
     * @param polygon Level-local XZ loop; the first vertex should not be repeated.
     * @return Applied on success; Rejected when the level is missing or geometry is invalid.
     */
    [[nodiscard]] eve::Result<void> createRoom(const std::string& levelId, const std::string& roomId,
                                               std::string roomName, std::vector<Vec2> polygon, double wallHeight,
                                               double wallThickness, double slabThickness);

    /**
     * @brief Insert a single wall under an existing level.
     * @return Applied on success; Rejected when the level is missing or geometry is invalid.
     */
    [[nodiscard]] eve::Result<void> createWall(const std::string& levelId, const std::string& wallId,
                                               std::string wallName, Vec2 start, Vec2 end, double height,
                                               double thickness);

    /**
     * @brief Insert a door/window opening on an existing wall.
     * @return Applied on success; Rejected when the wall is missing or parameters are invalid.
     */
    [[nodiscard]] eve::Result<void> createOpening(const std::string& wallId, const std::string& openingId,
                                                  std::string openingName, OpeningKind kind, double t, double width,
                                                  double height, double sill = 0.0);

    /**
     * @brief Place a catalog item on an existing level.
     * @return Applied on success; Rejected when the level is missing or placement is invalid.
     */
    [[nodiscard]] eve::Result<void> placeItem(const std::string& levelId, const std::string& itemId,
                                              std::string itemName, std::string catalogId, Vec3 position,
                                              double yawDegrees = 0.0);

    /** @brief Collect structural and geometric diagnostics as human-readable lines. */
    [[nodiscard]] std::vector<std::string> diagnostics() const;

    /**
     * @brief Build wall boxes (with opening cutouts), slabs and item markers for viewport upload.
     */
    [[nodiscard]] MeshBake bakeMesh() const;

    /** @brief Bake and pack into GPU-upload float arrays with per-triangle normals. */
    [[nodiscard]] MeshArrays bakeMeshArrays() const { return toMeshArrays(bakeMesh()); }

private:
    [[nodiscard]] bool canParent(NodeKind child, const std::string& parentId) const;
    [[nodiscard]] bool validateNode(const Node& node) const;
    void               unlinkFromParent(const std::string& id);
    void               collectDescendants(const std::string& id, std::vector<std::string>& out) const;

    std::string                           rootId_;
    std::unordered_map<std::string, Node> nodes_;
};

}  // namespace eve::archspace
