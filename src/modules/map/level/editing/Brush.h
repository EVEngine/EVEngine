#pragma once
#include "common/Export.h"


#include "map/level/editing/TileBuffer.h"

#include <string>
#include <vector>

namespace eve::level_editing {

/**
 * @brief Unity GridBrushBase-inspired tile brush.
 * Operates on TileBuffer; exposes preview + change list for UI / undo.
 */
class EVENGINE_API_DOMAINS Brush {
public:
    /** @brief Brush. */
    Brush();

    /** @brief Sets the tool. */
    void setTool(const std::string &tool);
    /** @brief Returns the tool. */
    std::string getTool() const { return tool_; }

    /** @brief Sets the size. */
    void setSize(int size);
    /** @brief Returns the size. */
    int getSize() const { return size_; }

    /** @brief Sets the shape. */
    void setShape(const std::string &shape);
    /** @brief Returns the shape. */
    std::string getShape() const { return shape_; }

    /** @brief Sets the tile. */
    void setTile(int gid);
    /** @brief Returns the tile. */
    int getTile() const { return tile_; }

    /** @brief Sets the erase tile. */
    void setEraseTile(int gid);
    /** @brief Returns the erase tile. */
    int getEraseTile() const { return eraseTile_; }

    /** @brief Sets the stamp size. */
    void setStampSize(int width, int height);
    /** @brief Returns the stamp width. */
    int getStampWidth() const { return stampW_; }
    /** @brief Returns the stamp height. */
    int getStampHeight() const { return stampH_; }
    /** @brief Sets the stamp tile. */
    void setStampTile(int lx, int ly, int gid);
    /** @brief Returns the stamp tile. */
    int getStampTile(int lx, int ly) const;
    /** @brief Clears stamp. */
    void clearStamp();

    /** @brief Apply current tool at / between cells. Returns cells changed. */
    int paintAt(TileBuffer *buffer, int tx, int ty);
    /** @brief Erases at. */
    int eraseAt(TileBuffer *buffer, int tx, int ty);
    /** @brief Flood fill. */
    int floodFill(TileBuffer *buffer, int tx, int ty);
    /** @brief Paint line. */
    int paintLine(TileBuffer *buffer, int x0, int y0, int x1, int y1);
    /** @brief Paint rect. */
    int paintRect(TileBuffer *buffer, int x0, int y0, int x1, int y1, bool filled);

    /** @brief Fill preview buffer without mutating target. */
    int previewAt(TileBuffer *buffer, int tx, int ty);
    /** @brief Preview line. */
    int previewLine(TileBuffer *buffer, int x0, int y0, int x1, int y1);
    /** @brief Preview rect. */
    int previewRect(TileBuffer *buffer, int x0, int y0, int x1, int y1, bool filled);

    /** @brief Returns the preview count. */
    int getPreviewCount() const { return static_cast<int>(preview_.size()); }
    /** @brief Returns the preview x. */
    int getPreviewX(int index) const;
    /** @brief Returns the preview y. */
    int getPreviewY(int index) const;
    /** @brief Returns the preview gid. */
    int getPreviewGid(int index) const;

    /** @brief Returns the change count. */
    int getChangeCount() const { return static_cast<int>(changes_.size()); }
    /** @brief Returns the change x. */
    int getChangeX(int index) const;
    /** @brief Returns the change y. */
    int getChangeY(int index) const;
    /** @brief Returns the change old gid. */
    int getChangeOldGid(int index) const;
    /** @brief Returns the change new gid. */
    int getChangeNewGid(int index) const;

private:
    struct Cell {
        int x = 0;
        int y = 0;
        int gid = 0;
        int oldGid = 0;
    };

    void clearChanges();
    void clearPreview();
    void collectBrushCells(int tx, int ty, std::vector<std::pair<int, int>> &out) const;
    void collectLineCells(int x0, int y0, int x1, int y1, std::vector<std::pair<int, int>> &out) const;
    void collectRectCells(int x0, int y0, int x1, int y1, bool filled,
                          std::vector<std::pair<int, int>> &out) const;
    int applyCells(TileBuffer *buffer, const std::vector<std::pair<int, int>> &cells, int gid);
    int applyStamp(TileBuffer *buffer, int tx, int ty);
    int previewCells(TileBuffer *buffer, const std::vector<std::pair<int, int>> &cells, int gid);
    bool validChange(int index) const;
    bool validPreview(int index) const;

    std::string tool_ = "paint";
    std::string shape_ = "square";
    int size_ = 1;
    int tile_ = 1;
    int eraseTile_ = 0;
    int stampW_ = 0;
    int stampH_ = 0;
    std::vector<int> stamp_;

    std::vector<Cell> changes_;
    std::vector<Cell> preview_;
};

}  // namespace eve::level_editing
