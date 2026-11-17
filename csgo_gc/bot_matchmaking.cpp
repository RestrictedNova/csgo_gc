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

std::vector<std::string> MapsFromSetting(std::string_view setting, RankedMode mode)
{
    std::vector<std::string> maps;

    while (!setting.empty())
    {
        const size_t comma = setting.find(',');
        std::string_view group = setting.substr(0, comma);
        setting = comma == std::string_view::npos ? std::string_view{} : setting.substr(comma + 1);

        constexpr std::string_view Prefix = "mg_";
        if (!group.starts_with(Prefix) || group == "mg_lobby_mapveto")
        {
            continue;
        }

        maps.emplace_back(group.substr(Prefix.size()));
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

} // namespace BotMatchmaking
