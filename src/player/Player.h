#pragma once
#include "Hand.h"
#include "tiles/TileEnums.h"
#include "game/GameState.h"
#include <functional>
#include <string>

class Player {
public:
    Player(int seatIndex, Wind seatWind, const std::string& name)
        : seatIndex_(seatIndex), seatWind_(seatWind), name_(name) {}
    virtual ~Player() = default;

    virtual bool isHuman() const = 0;

    // Request the player to choose a tile to discard
    virtual void requestDiscard(std::function<void(Tile)> callback) = 0;

    // Request the player to choose from available claim options (or pass)
    virtual void requestClaimDecision(
        Tile discardedTile,
        const std::vector<ClaimOption>& options,
        std::function<void(ClaimType)> callback) = 0;

    Hand& hand() { return hand_; }
    const Hand& hand() const { return hand_; }

    int seatIndex() const { return seatIndex_; }
    Wind seatWind() const { return seatWind_; }
    void setSeatWind(Wind w) { seatWind_ = w; }
    const std::string& name() const { return name_; }

    int score() const { return score_; }
    void adjustScore(int delta) { score_ += delta; }

    const std::vector<Tile>& discards() const { return discards_; }
    void addDiscard(Tile t) { discards_.push_back(t); }
    void removeLastDiscard() { if (!discards_.empty()) discards_.pop_back(); }
    void clearDiscards() { discards_.clear(); }

protected:
    int seatIndex_;
    Wind seatWind_;
    std::string name_;
    Hand hand_;
    int score_ = 0;
    std::vector<Tile> discards_;
};
