#pragma once

#include "progress.h"

// Official Matchmaking against bots (rank_progress): the bots play like players of a skill group.
// Matchmaking finds bots of the player's own skill group (from their rating), and the server
// loads the bot profiles for it (server.dll reads them from a file when a map starts) instead of
// Valve's easy to expert bots. A tier is a skill group, RankSilver1 to RankGlobalElite
namespace BotProfiles
{

// the skill group whose bots matchmaking finds for a rating
RankId TierFor(double rating);

// "Gold Nova II", for the log
std::string_view TierName(RankId tier);

// what beating or losing to them counts as in the rating: the middle of the skill group
double TierRating(RankId tier);

// how long players of the skill group take from seeing an enemy to damaging them, ms (csst.at's
// averages of Leetify stats, by FACEIT level, placed on the skill groups)
double TimeToDamage(RankId tier);

// 0 for silver I to 1 for the global elite, how far up the ladder of time to damage
double Strength(RankId tier);

// the settings a profile of the tier gets (botprofile.db's names)
struct Settings
{
    double reactionTime; // s, before the bot reacts to an enemy it sees
    double attackDelay; // s, more before it starts shooting
    double aimFocusInitial; // degrees its aim is off when it starts aiming
    double aimFocusDecay; // how much of that is left after a second
    double aimFocusOffsetScale; // how far off it aims at a moving target
    double aimFocusInterval; // s between corrections
    int skill; // 0-100, how well it plays
    int aggression; // 0-100
    int teamwork; // 0-100
    double lookAngleMaxAccelAttacking; // how fast it turns onto an enemy
    double lookAngleStiffnessAttacking;
    double lookAngleDampingAttacking;
};

Settings SettingsFor(RankId tier);

// a bot profile database with the tier's bots, in botprofile.db's format, every profile of
// difficulty expert (bot_difficulty 3 picks them)
std::string Database(RankId tier);

} // namespace BotProfiles
