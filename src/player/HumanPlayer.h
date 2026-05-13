#pragma once
#include "Player.h"

class HumanPlayer : public Player {
public:
    using Player::Player;

    bool isHuman() const override { return true; }

    void requestDiscard(std::function<void(Tile)> callback) override;
    void requestClaimDecision(
        Tile discardedTile,
        const std::vector<ClaimOption>& options,
        std::function<void(ClaimType)> callback) override;

    // Called by InputHandler when the human clicks a tile or button
    void onTileSelected(uint8_t tileId);
    void onClaimSelected(ClaimType type);

    bool isWaitingForDiscard() const { return pendingDiscardCb_ != nullptr; }
    bool isWaitingForClaim() const { return pendingClaimCb_ != nullptr; }

private:
    std::function<void(Tile)> pendingDiscardCb_;
    std::function<void(ClaimType)> pendingClaimCb_;
};
