#include "stdafx.h"
#include "bot_matchmaking.h"

#include <cmath>

namespace BotMatchmaking
{

// gamemodes.txt's mapgroupsMP of the final build: the active duty pool (competitive's lobby map
// veto isn't a map) and wingman's maps
constexpr std::string_view CompetitivePool[] = {
    "de_ancient", "de_anubis", "de_inferno", "de_mirage", "de_nuke", "de_overpass", "de_vertigo",
};

constexpr std::string_view WingmanPool[] = {
    "de_boyard", "de_chalice", "de_vertigo", "de_inferno", "de_overpass",
    "de_cbble", "de_train", "de_shortnuke", "de_shortdust", "de_lake",
};

std::optional<RankedMode> ModeFromSetting(std::string_view mode)
{
    if (mode == "competitive")
    {
        return RankedMode::Competitive;
    }

    if (mode == "scrimcomp2v2")
    {
        return RankedMode::Wingman;
    }

    return std::nullopt;
}

struct MapBit
{
    uint32_t bit;
    std::string_view map;
};

// client.dll's bit -> map tables for the game type, the branch for the newest match ids (the
// tables changed as maps came and went, older matches decode with older ones). competitive's
// lobby map veto isn't a map
constexpr MapBit CompetitiveBits[] = {
    { 0x2, "de_dust2" },
    { 0x4, "de_train" },
    { 0x8, "de_ancient" },
    { 0x10, "de_inferno" },
    { 0x20, "de_nuke" },
    { 0x40, "de_vertigo" },
    { 0x80, "de_mirage" },
    { 0x100, "cs_office" },
    { 0x200, "cs_italy" },
    { 0x400, "cs_assault" },
    { 0x800, "cs_militia" },
    { 0x1000, "de_cache" },
    { 0x4000, "de_breach" },
    { 0x8000, "de_anubis" },
    { 0x10000, "de_tuscan" },
    { 0x80000, "cs_agency" },
    { 0x100000, "de_overpass" },
    { 0x200000, "de_cbble" },
    { 0x400000, "de_canals" },
};

constexpr MapBit WingmanBits[] = {
    { 0x4, "de_train" },
    { 0x8, "de_lake" },
    { 0x10, "de_inferno" },
    { 0x20, "de_shortnuke" },
    { 0x40, "de_vertigo" },
    { 0x100, "de_prime" },
    { 0x200, "de_blagai" },
    { 0x400, "de_boyard" },
    { 0x800, "de_chalice" },
    { 0x8000, "de_shortdust" },
    { 0x100000, "de_overpass" },
    { 0x200000, "de_cbble" },
};

std::vector<std::string> MapsFromGameType(uint32_t gameType, RankedMode mode)
{
    const uint32_t bits = gameType >> 8;

    std::vector<std::string> maps;
    for (const MapBit &entry : mode == RankedMode::Wingman ? std::span<const MapBit>{ WingmanBits } : std::span<const MapBit>{ CompetitiveBits })
    {
        if (bits & entry.bit)
        {
            maps.emplace_back(entry.map);
        }
    }

    if (maps.empty())
    {
        if (mode == RankedMode::Wingman)
        {
            maps.assign(std::begin(WingmanPool), std::end(WingmanPool));
        }
        else
        {
            maps.assign(std::begin(CompetitivePool), std::end(CompetitivePool));
        }
    }

    return maps;
}

int BotDifficultyFor(double rating)
{
    // about a 70% chance to win against bots this far below you
    constexpr double Margin = 150;
    const double target = rating - Margin;

    int best = 0;
    for (int difficulty = 1; difficulty < 4; difficulty++)
    {
        if (std::fabs(Progress::BotRating(difficulty) - target) < std::fabs(Progress::BotRating(best) - target))
        {
            best = difficulty;
        }
    }

    return best;
}

int PlayerCount(RankedMode mode)
{
    return mode == RankedMode::Wingman ? 4 : 10;
}

std::string Reservation(uint64_t cookie, uint32_t accountId, RankedMode mode, int position)
{
    const int players = PlayerCount(mode);
    position = std::clamp(position, 0, players - 1);

    std::string reservation = std::format("Q{:x},1,1:", cookie);

    // made up account ids for the bots' places, never the player's
    uint32_t placeholder = 1;
    for (int i = 0; i < players; i++)
    {
        if (i == position)
        {
            reservation += std::format("[{:x}]", accountId);
            continue;
        }

        if (placeholder == accountId)
        {
            placeholder++;
        }

        reservation += std::format("[{:x}]", placeholder++);
    }

    return reservation;
}

} // namespace BotMatchmaking
