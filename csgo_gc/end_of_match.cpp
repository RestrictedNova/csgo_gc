#include "stdafx.h"
#include "end_of_match.h"

#include "cstrike15_usermessages.pb.h"

namespace EndOfMatch
{

std::string RankUpdateMessage(uint32_t accountId, RankedMode mode, const PlayerProgress::MatchUpdate &update)
{
    // the client picks out its own account's entry
    CCSUsrMsg_ServerRankUpdate message;
    CCSUsrMsg_ServerRankUpdate::RankUpdate *rank = message.add_rank_update();
    rank->set_account_id(static_cast<int32_t>(accountId));
    rank->set_rank_old(FromEnum(update.oldRank));
    rank->set_rank_new(FromEnum(update.newRank));
    rank->set_num_wins(update.wins);
    rank->set_rank_change(static_cast<float>(static_cast<int>(update.newRank) - static_cast<int>(update.oldRank)));
    rank->set_rank_type_id(mode == RankedMode::Wingman ? RankTypeWingman : RankTypeCompetitive);
    return message.SerializeAsString();
}

std::string XpUpdateMessage(uint32_t accountId, const PlayerProgress::MatchUpdate &update)
{
    // the screen fills the bar from the old XP with each reason in turn, levelling up if it gets
    // to the end
    CCSUsrMsg_XpUpdate message;
    CMsgGCCstrike15_v2_GC2ServerNotifyXPRewarded *data = message.mutable_data();
    data->set_account_id(accountId);
    data->set_current_level(update.oldLevel);
    data->set_current_xp(update.oldXp);

    auto add = [data](int category, int points)
    {
        if (points > 0)
        {
            XpProgressData *progress = data->add_xp_progress_data();
            progress->set_xp_points(points);
            progress->set_xp_category(category);
        }
    };

    add(XpEarned, update.xp.earned);
    add(XpWeeklyBonus, update.xp.bonus);
    add(XpEarnedReduced, update.xp.reduced);

    return message.SerializeAsString();
}

} // namespace EndOfMatch
