#pragma once

#include "gc_const_csgo.h"

// Level, XP and skill groups that change as you play matches against bots, saved to
// csgo_gc/progress.txt when "rank_progress" is on. A new file is a new account: level 1, no XP,
// no skill groups. Skill groups work like CS:GO's: a hidden Glicko-2 rating, shown after 10
// wins, hidden again ("expired") after 30 days without a win.

enum class RankedMode
{
    Competitive,
    Wingman
};

enum class MatchOutcome
{
    Win,
    Loss,
    Tie
};

// a finished match, from MatchTracker
struct MatchResult
{
    RankedMode mode{ RankedMode::Competitive };
    int botDifficulty{ 0 }; // bot_difficulty: 0 easy, 1 normal, 2 hard, 3 expert
    MatchOutcome outcome{ MatchOutcome::Loss };
    bool abandoned{ false }; // left before the end, a loss without XP like real matchmaking
    int roundsWon{ 0 };
    int roundsLost{ 0 };
    int kills{ 0 };
    int deaths{ 0 };
    int mvps{ 0 };
};

struct SkillRating
{
    double rating{ 1500 };
    double deviation{ 350 };
    double volatility{ 0.06 };
    int wins{ 0 };
    int matches{ 0 };
    uint32_t lastWinTime{ 0 };
};

namespace Progress
{

constexpr int WinsForSkillGroup = 10;
constexpr int XpPerLevel = 5000;
constexpr int MaxLevel = 40;
constexpr uint32_t SkillGroupExpiry = 30 * 24 * 60 * 60;

// the rating and deviation bots of a difficulty play at
double BotRating(int botDifficulty);
constexpr double BotDeviation = 50;

// one Glicko-2 rating period with a single game, score 1 win, 0.5 tie, 0 loss
void UpdateRating(SkillRating &skill, double opponentRating, double opponentDeviation, double score);

// the skill group a rating shows as, ignoring wins and expiry
RankId SkillGroupForRating(double rating);

// what the client should show: nothing before 10 wins or after 30 days without one
RankId VisibleSkillGroup(const SkillRating &skill, uint32_t now);

// XP for rounds won: 30 a round in competitive, 15 in wingman
int EarnedXp(RankedMode mode, int roundsWon);

// the weekly bonus: earned XP counts 4x until 4500 XP that week, 2x until 7500, 1x until
// 11167 and 0.175x after that. weekGained is what was gained this week before this match
int ApplyWeeklyBonus(int earned, int weekGained);

// the XP gained with the weekly bonus, split the way the end of match screen lists it: earned at
// the normal rate, the weekly bonus on top of that, and earned at the reduced rate once the
// week's bonus is used up
struct WeeklyXp
{
    int earned{ 0 };
    int bonus{ 0 };
    int reduced{ 0 };

    int Total() const { return earned + bonus + reduced; }
};

WeeklyXp SplitWeeklyXp(int earned, int weekGained);

// weeks start on wednesday 00:00 UTC
uint32_t WeekIndex(uint32_t time);

} // namespace Progress

class PlayerProgress
{
public:
    struct MatchUpdate
    {
        RankId oldRank;
        RankId newRank;
        double oldRating;
        double newRating;
        int wins; // in the mode, after this match
        int earnedXp; // rounds won x multiplier
        int gainedXp; // with the weekly bonus
        Progress::WeeklyXp xp; // gainedXp, split
        int oldLevel;
        int oldXp;
        int newLevel;
    };

    // a fresh account if the file doesn't exist
    void Load();
    void Save() const;

    int Level() const { return m_level; }
    int Xp() const { return m_xp; }
    void SetLevel(int level, int xp);

    const SkillRating &Skill(RankedMode mode) const;
    RankId SkillGroup(RankedMode mode, uint32_t now) const;

    MatchUpdate ApplyMatch(const MatchResult &result, uint32_t now);

private:
    SkillRating &MutableSkill(RankedMode mode);

    int m_level{ 1 };
    int m_xp{ 0 };
    uint32_t m_xpWeek{ 0 };
    int m_xpWeekGained{ 0 };
    SkillRating m_competitive;
    SkillRating m_wingman;
};
