#include "map/DualGrid.h"

namespace eve::map {
namespace {

// SpriteCook / common dual-grid 4×4 sheet: index by 4-bit corner mask.
// mask 0 → empty; other values map to atlas local ids 0..15.
constexpr std::array<int, 16> kDefaultFrameByMask = {
    -1, 15, 8, 9, 0, 11, 14, 7, 13, 4, 1, 10, 3, 2, 5, 6,
};

int resolveFirstGid(TileLayer *display, int firstDisplayGid) {
    if (firstDisplayGid > 0) return firstDisplayGid;
    const int ts = display->getTilesetFirstGid();
    return ts > 0 ? ts : 1;
}

}  // namespace

const std::array<int, 16> &dualGridDefaultFrameTable() { return kDefaultFrameByMask; }

int dualGridDefaultFrame(int mask) {
    if (mask < 0 || mask > 15) return -1;
    return kDefaultFrameByMask[size_t(mask)];
}

bool dualGridLogicFilled(TileLayer &logic, int tx, int ty, int filledGid) {
    auto *cfg = logic.config();
    if (!cfg || tx < 0 || ty < 0 || tx >= cfg->mapW || ty >= cfg->mapH) return false;
    const int gid = int(tileGid(uint32_t(logic.getTile(tx, ty))));
    if (gid == 0) return false;
    if (filledGid == 0) return true;
    return gid == filledGid;
}

int dualGridMaskAt(TileLayer &logic, int dx, int dy, int filledGid) {
    const bool tl = dualGridLogicFilled(logic, dx - 1, dy - 1, filledGid);
    const bool tr = dualGridLogicFilled(logic, dx, dy - 1, filledGid);
    const bool bl = dualGridLogicFilled(logic, dx - 1, dy, filledGid);
    const bool br = dualGridLogicFilled(logic, dx, dy, filledGid);
    return dualGridMaskFromCorners(tl, tr, bl, br);
}

bool resolveDualGrid(TileLayer *logic, TileLayer *display, const DualGridOptions &opts,
                     std::string *error) {
    if (!logic || !display) {
        if (error) *error = "resolveDualGrid: logic and display layers required";
        return false;
    }
    if (logic == display) {
        if (error) *error = "resolveDualGrid: logic and display must be different layers";
        return false;
    }

    const int logicW = logic->getMapWidth();
    const int logicH = logic->getMapHeight();
    if (logicW <= 0 || logicH <= 0) {
        if (error) *error = "resolveDualGrid: logic layer has empty size";
        return false;
    }

    const float tileW = logic->getTileWidth();
    const float tileH = logic->getTileHeight();
    display->setTileSize(tileW, tileH);
    display->resize(logicW + 1, logicH + 1);

    auto *lc = logic->config();
    auto *dc = display->config();
    dc->orientation = lc->orientation;
    dc->staggerAxis = lc->staggerAxis;
    dc->staggerIndex = lc->staggerIndex;
    dc->hexSideLength = lc->hexSideLength;

    if (opts.applyHalfOffset) {
        display->setOrigin(logic->getX() - tileW * 0.5f, logic->getY() - tileH * 0.5f);
    } else {
        display->setOrigin(logic->getX(), logic->getY());
    }

    // Keep draw bookkeeping in sync when display is freshly created.
    display->setLayer(logic->getLayer());
    display->setCamera(logic->draw()->camera);
    display->setCanvas(logic->draw()->canvas);

    const int firstGid = resolveFirstGid(display, opts.firstDisplayGid);
    const int filledGid = opts.filledGid;

    for (int dy = 0; dy < logicH + 1; ++dy) {
        for (int dx = 0; dx < logicW + 1; ++dx) {
            const int mask = dualGridMaskAt(*logic, dx, dy, filledGid);
            int frame = -1;
            if (opts.useDefaultFrameTable) {
                frame = dualGridDefaultFrame(mask);
            } else if (mask != 0) {
                frame = mask;
            }
            const int gid = (frame < 0) ? 0 : firstGid + frame;
            display->setTile(dx, dy, gid);
        }
    }

    if (opts.hideLogic) logic->setVisible(false);
    display->setVisible(true);

    if (error) error->clear();
    return true;
}

bool resolveDualGrid(TileLayer *logic, TileLayer *display, std::string *error) {
    return resolveDualGrid(logic, display, DualGridOptions{}, error);
}

}  // namespace eve::map
