#pragma once
#include "tiles/Tile.h"
#include "tiles/TileEnums.h"
#include "player/Meld.h"
#include "scoring/Scoring.h"
#include "game/GameState.h"
#include "GameSnapshot.h"
#include <sstream>
#include <string>
#include <vector>

namespace json {

inline std::string escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

inline void appendTile(std::ostringstream& os, const Tile& t) {
    os << "{\"suit\":" << static_cast<int>(t.suit)
       << ",\"rank\":" << static_cast<int>(t.rank)
       << ",\"id\":" << static_cast<int>(t.id)
       << ",\"name\":\"" << escape(t.englishName()) << "\"}";
}

inline void appendMeld(std::ostringstream& os, const Meld& m) {
    os << "{\"type\":" << static_cast<int>(m.type)
       << ",\"exposed\":" << (m.exposed ? "true" : "false")
       << ",\"tiles\":[";
    for (size_t i = 0; i < m.tiles.size(); i++) {
        if (i > 0) os << ",";
        appendTile(os, m.tiles[i]);
    }
    os << "]}";
}

inline void appendTileArray(std::ostringstream& os, const std::vector<Tile>& tiles) {
    os << "[";
    for (size_t i = 0; i < tiles.size(); i++) {
        if (i > 0) os << ",";
        appendTile(os, tiles[i]);
    }
    os << "]";
}

inline void appendMeldArray(std::ostringstream& os, const std::vector<Meld>& melds) {
    os << "[";
    for (size_t i = 0; i < melds.size(); i++) {
        if (i > 0) os << ",";
        appendMeld(os, melds[i]);
    }
    os << "]";
}

inline const char* claimTypeToString(ClaimType type) {
    switch (type) {
        case ClaimType::Chow: return "chow";
        case ClaimType::Pung: return "pung";
        case ClaimType::Kong: return "kong";
        case ClaimType::Win:  return "win";
        case ClaimType::None: return "none";
        default: return "none";
    }
}

inline void appendFaanResult(std::ostringstream& os, const FaanResult& result) {
    os << "{\"totalFaan\":" << result.totalFaan
       << ",\"isLimit\":" << (result.isLimit ? "true" : "false")
       << ",\"breakdown\":[";
    for (size_t i = 0; i < result.breakdown.size(); i++) {
        if (i > 0) os << ",";
        const auto& entry = result.breakdown[i];
        os << "{\"nameCn\":\"" << escape(entry.nameCn) << "\""
           << ",\"nameEn\":\"" << escape(entry.nameEn) << "\""
           << ",\"faan\":" << entry.faan << "}";
    }
    os << "]}";
}

inline void appendClaimOption(std::ostringstream& os, const ClaimOption& opt, int index) {
    os << "{\"index\":" << index
       << ",\"type\":\"" << claimTypeToString(opt.type) << "\""
       << ",\"meldTiles\":[";
    for (size_t i = 0; i < opt.meldTiles.size(); i++) {
        if (i > 0) os << ",";
        appendTile(os, opt.meldTiles[i]);
    }
    os << "]}";
}

inline void appendSelfKongEntry(std::ostringstream& os, const SelfKongEntry& e) {
    os << "{\"suit\":" << static_cast<int>(e.suit)
       << ",\"rank\":" << static_cast<int>(e.rank)
       << ",\"isPromote\":" << (e.isPromote ? "true" : "false") << "}";
}

inline void appendScoringSnapshot(std::ostringstream& os, const ScoringSnapshot& s) {
    os << "{\"winnerIndex\":" << s.winnerIndex
       << ",\"selfDrawn\":" << (s.selfDrawn ? "true" : "false")
       << ",\"isDraw\":" << (s.isDraw ? "true" : "false")
       << ",\"totalFaan\":" << s.totalFaan
       << ",\"isLimit\":" << (s.isLimit ? "true" : "false")
       << ",\"basePoints\":" << s.basePoints
       << ",\"breakdown\":[";
    for (size_t i = 0; i < s.breakdown.size(); i++) {
        if (i > 0) os << ",";
        const auto& e = s.breakdown[i];
        os << "{\"nameCn\":\"" << escape(e.nameCn) << "\""
           << ",\"nameEn\":\"" << escape(e.nameEn) << "\""
           << ",\"faan\":" << e.faan << "}";
    }
    os << "]}";
}

} // namespace json
