#include "HumanPlayer.h"
#include <algorithm>

void HumanPlayer::requestDiscard(std::function<void(Tile)> callback) {
    pendingDiscardCb_ = std::move(callback);
}

void HumanPlayer::requestClaimDecision(
    Tile /*discardedTile*/,
    const std::vector<ClaimOption>& /*options*/,
    std::function<void(ClaimType)> callback) {
    pendingClaimCb_ = std::move(callback);
}

void HumanPlayer::onTileSelected(uint8_t tileId) {
    if (!pendingDiscardCb_) return;

    auto& tiles = hand().concealed();
    auto it = std::find_if(tiles.begin(), tiles.end(),
        [tileId](const Tile& t) { return t.id == tileId; });
    if (it != tiles.end()) {
        Tile selected = *it;
        auto cb = std::move(pendingDiscardCb_);
        pendingDiscardCb_ = nullptr;
        cb(selected);
    }
}

void HumanPlayer::onClaimSelected(ClaimType type) {
    if (!pendingClaimCb_) return;
    auto cb = std::move(pendingClaimCb_);
    pendingClaimCb_ = nullptr;
    cb(type);
}
