#pragma once

#include "progress.h"

// The skill group and XP screens at the end of a match against bots (rank_progress). Official
// servers passed the gc's rank and XP updates on to the client as user messages, which is what
// those screens show, but the final server.dll no longer does, so csgo_gc hands the client the
// same messages itself (MatchWatcher::DispatchUserMessage)
namespace EndOfMatch
{

// the XP reasons the screen lists (XP_Bonus_RankUp_<category> in csgo_english.txt)
constexpr int XpEarned = 2; // "Earned (Rounds Won x Multiplier)"
constexpr int XpWeeklyBonus = 3; // "Weekly Bonus"
constexpr int XpEarnedReduced = 52; // "Earned (Reduced)", once the week's bonus is used up

// CCSUsrMsg_ServerRankUpdate (CS_UM_ServerRankUpdate) with the player's skill group before and
// after the match and their wins, serialized
std::string RankUpdateMessage(uint32_t accountId, RankedMode mode, const PlayerProgress::MatchUpdate &update);

// CCSUsrMsg_XpUpdate (CS_UM_XpUpdate): the level and XP before the match and the XP it gave,
// serialized
std::string XpUpdateMessage(uint32_t accountId, const PlayerProgress::MatchUpdate &update);

} // namespace EndOfMatch
