#include "stdafx.h"
#include "progress.h"
#include "keyvalue.h"

#include <cmath>

constexpr const char *ProgressFilePath = "csgo_gc/progress.txt";

namespace Progress
{

// how good bots of each difficulty count as. winning 95% against a difficulty settles you about
// 500 above it, and wins never take you more than MaxAboveBot above the bots you beat: easy
// tops out at Gold Nova II, normal Master Guardian II, hard Legendary Eagle, expert The Global Elite
constexpr double BotRatings[] = { 1000, 1300, 1550, 1800 };

// without it a perfect record keeps climbing forever, Glicko-2 gets less sure of a rating that
// only ever sees expected results
constexpr double MaxAboveBot = 520;

// lowest rating of Silver II through The Global Elite, below the first one is Silver I
constexpr double SkillGroupFloors[] = {
    1020, 1100, 1180, 1260, 1340, 1420, 1500, 1580, 1660,
    1740, 1820, 1900, 1980, 2060, 2140, 2220, 2300,
};

constexpr double GlickoScale = 173.7178;
constexpr double Tau = 0.5;
constexpr double Pi = 3.14159265358979323846;

double BotRating(int botDifficulty)
{
    return BotRatings[std::clamp(botDifficulty, 0, static_cast<int>(std::size(BotRatings)) - 1)];
}

// Glickman's "Example of the Glicko-2 system", one opponent per period
void UpdateRating(SkillRating &skill, double opponentRating, double opponentDeviation, double score)
{
    const double mu = (skill.rating - 1500) / GlickoScale;
    const double phi = skill.deviation / GlickoScale;
    const double sigma = skill.volatility;
    const double muJ = (opponentRating - 1500) / GlickoScale;
    const double phiJ = opponentDeviation / GlickoScale;

    const double g = 1 / std::sqrt(1 + 3 * phiJ * phiJ / (Pi * Pi));
    const double e = std::clamp(1 / (1 + std::exp(-g * (mu - muJ))), 1e-9, 1 - 1e-9);
    const double v = 1 / (g * g * e * (1 - e));
    const double delta = v * g * (score - e);

    // the new volatility, with the Illinois algorithm
    const double a = std::log(sigma * sigma);
    auto f = [&](double x)
    {
        const double ex = std::exp(x);
        const double d = phi * phi + v + ex;
        return ex * (delta * delta - phi * phi - v - ex) / (2 * d * d) - (x - a) / (Tau * Tau);
    };

    double A = a;
    double B;
    if (delta * delta > phi * phi + v)
    {
        B = std::log(delta * delta - phi * phi - v);
    }
    else
    {
        int k = 1;
        while (f(a - k * Tau) < 0 && k < 100)
        {
            k++;
        }

        B = a - k * Tau;
    }

    double fA = f(A);
    double fB = f(B);
    for (int i = 0; i < 100 && std::fabs(B - A) > 1e-6; i++)
    {
        const double C = A + (A - B) * fA / (fB - fA);
        const double fC = f(C);
        if (fC * fB <= 0)
        {
            A = B;
            fA = fB;
        }
        else
        {
            fA /= 2;
        }

        B = C;
        fB = fC;
    }

    const double newSigma = std::exp(A / 2);
    const double phiStar = std::sqrt(phi * phi + newSigma * newSigma);
    const double newPhi = 1 / std::sqrt(1 / (phiStar * phiStar) + 1 / v);
    const double newMu = mu + newPhi * newPhi * g * (score - e);

    skill.rating = newMu * GlickoScale + 1500;
    skill.deviation = std::min(newPhi * GlickoScale, 350.0);
    skill.volatility = newSigma;
}

RankId SkillGroupForRating(double rating)
{
    uint32_t group = RankSilver1;
    for (double floor : SkillGroupFloors)
    {
        if (rating >= floor)
        {
            group++;
        }
    }

    return static_cast<RankId>(group);
}

double SkillGroupFloor(RankId group)
{
    const double width = SkillGroupFloors[1] - SkillGroupFloors[0];
    const int index = static_cast<int>(group) - static_cast<int>(RankSilver2);
    if (index < 0)
    {
        return SkillGroupFloors[0] - width;
    }

    return SkillGroupFloors[std::min(index, static_cast<int>(std::size(SkillGroupFloors)) - 1)];
}

RankId VisibleSkillGroup(const SkillRating &skill, uint32_t now)
{
    if (skill.wins < WinsForSkillGroup)
    {
        return RankNone;
    }

    // the client shows "Skill Group expired" for enough wins and no skill group
    if (now > skill.lastWinTime && now - skill.lastWinTime > SkillGroupExpiry)
    {
        return RankNone;
    }

    return SkillGroupForRating(skill.rating);
}

int EarnedXp(RankedMode mode, int roundsWon)
{
    return std::max(roundsWon, 0) * (mode == RankedMode::Wingman ? 15 : 30);
}

int ApplyWeeklyBonus(int earned, int weekGained)
{
    return SplitWeeklyXp(earned, weekGained).Total();
}

WeeklyXp SplitWeeklyXp(int earned, int weekGained)
{
    struct Tier
    {
        double end; // weekly XP this tier lasts until
        double multiplier;
    };

    constexpr Tier Tiers[] = {
        { 4500, 4 },
        { 7500, 2 },
        { 11167, 1 },
        { HUGE_VAL, 0.175 },
    };

    double remaining = earned;
    double week = weekGained;
    double normal = 0;
    double bonus = 0;
    double reduced = 0;

    for (const Tier &tier : Tiers)
    {
        if (remaining <= 0)
        {
            break;
        }

        if (week >= tier.end)
        {
            continue;
        }

        // earned XP it takes to fill the rest of this tier
        const double fill = (tier.end - week) / tier.multiplier;
        const double used = std::min(remaining, fill);
        if (tier.multiplier >= 1)
        {
            normal += used;
            bonus += used * (tier.multiplier - 1);
        }
        else
        {
            reduced += used * tier.multiplier;
        }

        week += used * tier.multiplier;
        remaining -= used;
    }

    WeeklyXp xp;
    xp.earned = static_cast<int>(std::lround(normal));
    xp.bonus = static_cast<int>(std::lround(bonus));
    xp.reduced = static_cast<int>(std::lround(reduced));
    return xp;
}

uint32_t WeekIndex(uint32_t time)
{
    constexpr uint32_t FirstWednesday = 6 * 24 * 60 * 60; // 1970-01-07
    constexpr uint32_t Week = 7 * 24 * 60 * 60;
    return time < FirstWednesday ? 0 : (time - FirstWednesday) / Week + 1;
}

} // namespace Progress

static void ReadSkill(const KeyValue *key, SkillRating &skill)
{
    if (!key)
    {
        return;
    }

    skill.rating = key->GetNumber<float>("rating", static_cast<float>(skill.rating));
    skill.deviation = key->GetNumber<float>("deviation", static_cast<float>(skill.deviation));
    skill.volatility = key->GetNumber<float>("volatility", static_cast<float>(skill.volatility));
    skill.wins = key->GetNumber<int>("wins", skill.wins);
    skill.matches = key->GetNumber<int>("matches", skill.matches);
    skill.lastWinTime = key->GetNumber<uint32_t>("last_win_time", skill.lastWinTime);
}

static void WriteSkill(KeyValueWrite &write, std::string_view name, const SkillRating &skill)
{
    write.BeginBlock(name);
    write.AddString("rating", FloatToString(static_cast<float>(skill.rating)));
    write.AddString("deviation", FloatToString(static_cast<float>(skill.deviation)));
    write.AddString("volatility", FloatToString(static_cast<float>(skill.volatility)));
    write.AddNumber("wins", skill.wins);
    write.AddNumber("matches", skill.matches);
    write.AddNumber("last_win_time", skill.lastWinTime);
    write.EndBlock();
}

void PlayerProgress::Load()
{
    KeyValueRead read{ ProgressFilePath };
    if (!read.Good())
    {
        Platform::Print("No {}, starting a new account: level 1, no skill groups\n", ProgressFilePath);
        return;
    }

    const KeyValue &root = read.Root();
    m_level = std::clamp(root.GetNumber<int>("level", m_level), 1, Progress::MaxLevel);
    m_xp = std::clamp(root.GetNumber<int>("xp", m_xp), 0, Progress::XpPerLevel - 1);
    m_xpWeek = root.GetNumber<uint32_t>("xp_week", m_xpWeek);
    m_xpWeekGained = root.GetNumber<int>("xp_week_gained", m_xpWeekGained);
    ReadSkill(root.GetSubkey("competitive"), m_competitive);
    ReadSkill(root.GetSubkey("wingman"), m_wingman);
}

void PlayerProgress::Save() const
{
    FILE *f = fopen(ProgressFilePath, "wb");
    if (!f)
    {
        Platform::Print("Could not open {} for writing\n", ProgressFilePath);
        return;
    }

    KeyValueWrite write{ f };
    write.AddNumber("level", m_level);
    write.AddNumber("xp", m_xp);
    write.AddNumber("xp_week", m_xpWeek);
    write.AddNumber("xp_week_gained", m_xpWeekGained);
    WriteSkill(write, "competitive", m_competitive);
    WriteSkill(write, "wingman", m_wingman);

    if (ferror(f) != 0 || fclose(f) == EOF)
    {
        Platform::Print("Writing {} failed\n", ProgressFilePath);
    }
}

void PlayerProgress::SetLevel(int level, int xp)
{
    m_level = std::clamp(level, 1, Progress::MaxLevel);
    m_xp = std::clamp(xp, 0, Progress::XpPerLevel - 1);
}

const SkillRating &PlayerProgress::Skill(RankedMode mode) const
{
    return mode == RankedMode::Wingman ? m_wingman : m_competitive;
}

SkillRating &PlayerProgress::MutableSkill(RankedMode mode)
{
    return mode == RankedMode::Wingman ? m_wingman : m_competitive;
}

RankId PlayerProgress::SkillGroup(RankedMode mode, uint32_t now) const
{
    return Progress::VisibleSkillGroup(Skill(mode), now);
}

PlayerProgress::MatchUpdate PlayerProgress::ApplyMatch(const MatchResult &result, uint32_t now)
{
    MatchUpdate update{};
    SkillRating &skill = MutableSkill(result.mode);

    update.oldRank = Progress::VisibleSkillGroup(skill, now);
    update.oldRating = skill.rating;

    const bool won = !result.abandoned && result.outcome == MatchOutcome::Win;
    double score = 0;
    if (won)
    {
        score = 1;
    }
    else if (!result.abandoned && result.outcome == MatchOutcome::Tie)
    {
        score = 0.5;
    }

    const double botRating = result.botRating > 0 ? result.botRating : Progress::BotRating(result.botDifficulty);
    Progress::UpdateRating(skill, botRating, Progress::BotDeviation, score);
    if (score > 0)
    {
        // beating bots you're far above doesn't take you higher, but doesn't drag you down either
        skill.rating = std::min(skill.rating, std::max(update.oldRating, botRating + Progress::MaxAboveBot));
    }

    skill.matches++;
    if (won)
    {
        skill.wins++;
        skill.lastWinTime = now;
    }

    update.newRank = Progress::VisibleSkillGroup(skill, now);
    update.newRating = skill.rating;
    update.wins = skill.wins;

    // XP only for matches played to the end
    update.oldLevel = m_level;
    update.oldXp = m_xp;
    if (!result.abandoned)
    {
        const uint32_t week = Progress::WeekIndex(now);
        if (week != m_xpWeek)
        {
            m_xpWeek = week;
            m_xpWeekGained = 0;
        }

        update.earnedXp = Progress::EarnedXp(result.mode, result.roundsWon);
        update.xp = Progress::SplitWeeklyXp(update.earnedXp, m_xpWeekGained);
        update.gainedXp = update.xp.Total();
        m_xpWeekGained += update.gainedXp;

        m_xp += update.gainedXp;
        while (m_xp >= Progress::XpPerLevel && m_level < Progress::MaxLevel)
        {
            m_xp -= Progress::XpPerLevel;
            m_level++;
        }

        // at the top level the client offers a service medal, the bar stays full until then
        if (m_level >= Progress::MaxLevel)
        {
            m_xp = std::min(m_xp, Progress::XpPerLevel - 1);
        }
    }

    update.newLevel = m_level;
    return update;
}
