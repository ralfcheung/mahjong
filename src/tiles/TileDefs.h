#pragma once

// Chinese character mappings for tile faces (bilingual)

// Characters suit (萬子)
inline const char* CHAR_WAN_CN[] = {
    "一萬", "二萬", "三萬", "四萬", "五萬",
    "六萬", "七萬", "八萬", "九萬"
};
inline const char* CHAR_WAN_EN[] = {
    "1 Wan", "2 Wan", "3 Wan", "4 Wan", "5 Wan",
    "6 Wan", "7 Wan", "8 Wan", "9 Wan"
};

// Bamboo suit (索子)
inline const char* CHAR_BAMBOO_CN[] = {
    "一索", "二索", "三索", "四索", "五索",
    "六索", "七索", "八索", "九索"
};
inline const char* CHAR_BAMBOO_EN[] = {
    "1 Sok", "2 Sok", "3 Sok", "4 Sok", "5 Sok",
    "6 Sok", "7 Sok", "8 Sok", "9 Sok"
};

// Dots suit (筒子)
inline const char* CHAR_DOTS_CN[] = {
    "一筒", "二筒", "三筒", "四筒", "五筒",
    "六筒", "七筒", "八筒", "九筒"
};
inline const char* CHAR_DOTS_EN[] = {
    "1 Tung", "2 Tung", "3 Tung", "4 Tung", "5 Tung",
    "6 Tung", "7 Tung", "8 Tung", "9 Tung"
};

// Wind tiles (風牌)
inline const char* CHAR_WIND_CN[] = { "東", "南", "西", "北" };
inline const char* CHAR_WIND_EN[] = { "East", "South", "West", "North" };

// Dragon tiles (三元牌)
inline const char* CHAR_DRAGON_CN[] = { "中", "發", "白" };
inline const char* CHAR_DRAGON_EN[] = { "Red", "Green", "White" };

// Flower tiles (花牌)
inline const char* CHAR_FLOWER_CN[] = { "梅", "蘭", "菊", "竹" };
inline const char* CHAR_FLOWER_EN[] = { "Plum", "Orchid", "Chrys", "Bamboo" };

// Season tiles (季牌)
inline const char* CHAR_SEASON_CN[] = { "春", "夏", "秋", "冬" };
inline const char* CHAR_SEASON_EN[] = { "Spring", "Summer", "Autumn", "Winter" };

// Scoring pattern names (bilingual)
struct BilingualString {
    const char* cn;
    const char* en;
};

// Faan pattern display names
inline const BilingualString FAAN_NAMES[] = {
    {"自摸", "Self-Draw"},
    {"門前清", "Concealed Hand"},
    {"無花", "No Flowers"},
    {"正花", "Seat Flower"},
    {"番牌", "Dragon Pung"},
    {"門風", "Seat Wind"},
    {"圈風", "Prevailing Wind"},
    {"搶槓", "Robbing the Kong"},
    {"海底撈月", "Last Tile Win"},
    {"槓上開花", "Win on Kong"},
    {"花糊", "All Flowers/Seasons"},
    {"對對糊", "All Pongs"},
    {"混一色", "Half Flush"},
    {"小三元", "Little Three Dragons"},
    {"七對子", "Seven Pairs"},
    {"清一色", "Full Flush"},
    {"大三元", "Great Three Dragons"},
    {"小四喜", "Little Four Winds"},
    {"大四喜", "Great Four Winds"},
    {"十三么", "Thirteen Orphans"},
    {"九子連環", "Nine Gates"},
    {"字一色", "All Honors"},
    {"清么九", "All Terminals"},
    {"四暗刻", "Four Concealed Pongs"},
    {"十八羅漢", "All Kongs"},
    {"天糊", "Heavenly Hand"},
    {"地糊", "Earthly Hand"},
    {"平糊", "All Chows"},
};

// UI labels (bilingual)
inline const BilingualString UI_CHOW   = {"上", "Chow"};
inline const BilingualString UI_PUNG   = {"碰", "Pung"};
inline const BilingualString UI_KONG   = {"槓", "Kong"};
inline const BilingualString UI_WIN    = {"食糊", "Win!"};
inline const BilingualString UI_PASS   = {"過", "Pass"};
inline const BilingualString UI_DRAW   = {"摸牌", "Draw"};

inline const BilingualString UI_SEAT_NAMES[] = {
    {"東家", "East"},
    {"南家", "South"},
    {"西家", "West"},
    {"北家", "North"},
};

inline const BilingualString UI_ROUND_WIND = {"圈風", "Round Wind"};
inline const BilingualString UI_SEAT_WIND  = {"門風", "Seat Wind"};
