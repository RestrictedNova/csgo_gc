#pragma once

#include "progress.h"

// Official Matchmaking against bots (rank_progress): what the GC decides when you press Go.
// Your mode and maps come from the play menu's settings, the bots from your skill.
namespace BotMatchmaking
{

// ui_playsettings_mode_official: "competitive" or "scrimcomp2v2" (wingman), nothing for modes
// bots can't be ranked in
std::optional<RankedMode> ModeFromSetting(std::string_view mode);

// ui_playsettings_maps_official_<mode>, the map groups ticked in the play menu
// ("mg_de_dust2,mg_de_mirage"). the mode's active maps when none are
std::vector<std::string> MapsFromSetting(std::string_view setting, RankedMode mode);

// matchmaking finds opponents near your skill: the difficulty whose bots are closest to a bit
// below your rating, so you win most matches but not all of them
int BotDifficultyFor(double rating);

} // namespace BotMatchmaking
