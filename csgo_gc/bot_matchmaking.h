#pragma once

#include "progress.h"

// Official Matchmaking against bots (rank_progress): what the GC decides when you press Go.
// Your mode and maps come from the play menu's settings, the bots from your skill.
namespace BotMatchmaking
{

// ui_playsettings_mode_official: "competitive" or "scrimcomp2v2" (wingman), nothing for modes
// bots can't be ranked in
std::optional<RankedMode> ModeFromSetting(std::string_view mode);

// competitive's match length (ui_playsettings_flags_official_competitive)
constexpr uint32_t FlagsLongMatch = 16;
constexpr uint32_t FlagsShortMatch = 32;
constexpr uint32_t FlagsAnyLength = FlagsLongMatch | FlagsShortMatch;

// the maps ticked in the play menu, from MatchmakingStart's game type: the low byte is the mode,
// the rest a bit per map (which bit is which depends on the mode, client.dll's table). the
// mode's active maps when none are
std::vector<std::string> MapsFromGameType(uint32_t gameType, RankedMode mode);

// matchmaking finds opponents near your skill: the difficulty whose bots are closest to a bit
// below your rating, so you win most matches but not all of them
int BotDifficultyFor(double rating);

// players in a match: 10 in competitive, 4 in wingman
int PlayerCount(RankedMode mode);

// sv_mmqueue_reservation for an official match, which is what makes the local server run as an
// official matchmaking server: "Q<cookie>,1,1:" then an "[account id]" (hex) per player. The
// server puts the first half of the list on one team and the second half on the other, so the
// player's position picks their team, and bots take the places of the players nobody has
std::string Reservation(uint64_t cookie, uint32_t accountId, RankedMode mode, int position);

} // namespace BotMatchmaking
