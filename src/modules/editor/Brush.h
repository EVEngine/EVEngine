#pragma once

#include "editor/TileBuffer.h"

#include <string>
#include <vector>

namespace eve::editor {

/**
 * @brief Unity GridBrushBase-inspired tile brush.
 * Operates on TileBuffer; exposes preview + change list for UI / undo.
 */
class Brush {
public:
    Brush();

    void setTool(const std::string &tool);
    std::string getTool() const { return tool_; }

    void setSize(int size);
    int getSize() const { return size_; }

    void setShape(const std::string &shape);
    std::string getShape() const { return shape_; }

    void setTile(int gid);
    int getTile() const { return tile_; }

    void setEraseTile(int gid);
    int getEraseTile() const { return eraseTile_; }

    void setStampSize(int width, int height);
    int getStampWidth() const { return stampW_; }
    int getStampHeight() const { return stampH_; }
    void setStampTile(int lx, int ly, int gid);
    int getStampTile(int lx, int ly) const;
    void clearStamp();

    /** @brief Apply current tool at / between cells. Returns cells changed. */
    int paintAt(TileBuffer *buffer, int tx, int ty);
    int eraseAt(TileBuffer *buffer, int tx, int ty);
    int floodFill(TileBuffer *buffer, int tx, int ty);
    int paintLine(TileBuffer *buffer, int x0, int y0, int x1, int y1);
    int paintRect(TileBuffer *buffer, int x0, int y0, int x1, int y1, bool filled);

    /** @brief Fill preview buffer without mutating target. */
    int previewAt(TileBuffer *buffer, int tx, int ty);
    int previewLine(TileBuffer *buffer, int x0, int y0, int x1, int y1);
    int previewRect(TileBuffer *buffer, int x0, int y0, int x1, int y1, bool filled);

    int getPreviewCount() const { return static_cast<int>(preview_.size()); }
    int getPreviewX(int index) const;
    int getPreviewY(int index) const;
    int getPreviewGid(int index) const;

    int getChangeCount() const { return static_cast<int>(changes_.size()); }
    int getChangeX(int index) const;
    int getChangeY(int index) const;
    int getChangeOldGid(int index) const;
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

}  // namespace eve::editor
