#pragma once

#include "progress.h"

#include <functional>

// Follows a match against bots on a local server (Practice with bots) from the server's game
// events and produces its result when it ends, or when the player leaves it early.

// what the tracker reads from a game event
class MatchEvent
{
public:
    virtual std::string_view Name() const = 0;
    virtual int GetInt(const char *key) const = 0;
    virtual bool GetBool(const char *key) const = 0;
    virtual std::string_view GetString(const char *key) const = 0;
};

// read when a match starts
struct MatchSettings
{
    int gameType{ -1 };
    int gameMode{ -1 };
    int botDifficulty{ 0 };
};

class MatchTracker
{
public:
    // the competitive or wingman mode a match is ranked in, nothing for other modes
    static std::optional<RankedMode> RankedModeFor(const MatchSettings &settings);

    // feed every game event. settings are read when a match goes live (begin_new_match)
    std::optional<MatchResult> OnEvent(const MatchEvent &event, const std::function<MatchSettings()> &readSettings);

    // the server went away (map change, disconnect) with the match still running
    std::optional<MatchResult> OnServerGone();

private:
    std::optional<MatchResult> Finish(bool abandoned);

    // the one person playing, nothing if we don't know them or there are more
    std::optional<int> HumanUserId() const;

    struct Player
    {
        int team{ 0 };
        bool bot{ false };
        bool known{ false }; // seen joining or picking a team, so bot is right
    };

    std::unordered_map<int, Player> m_players; // by userid
    bool m_live{ false };
    MatchSettings m_settings;
    RankedMode m_mode{ RankedMode::Competitive };
    int m_roundsWon{ 0 };
    int m_roundsLost{ 0 };
    int m_kills{ 0 };
    int m_deaths{ 0 };
    int m_mvps{ 0 };
};

class SharedGC;

// watches the game's events and sends finished matches to the client gc, windows only for now
namespace MatchWatcher
{

// call every frame on the main thread
void Update(SharedGC &gc);

// stop listening, the gc is going away
void Shutdown();

// console variables, which is where the play menu keeps its settings. main thread only, empty
// or nothing when the engine isn't there
std::string GetGameSetting(const char *name);
void SetGameSetting(const char *name, const char *value);

// a match found by Official Matchmaking is about to start on this machine. once the player is in
// the next local server, it becomes an official matchmaking server: the reservation (from
// BotMatchmaking::Reservation) puts them on their team, bots of this difficulty fill the
// others' places and the warmup is the one official servers had once everyone connected
struct OfficialMatchSettings
{
    std::string reservation;
    int botDifficulty{ 0 };
    int players{ 10 };
    bool shortMatch{ false }; // competitive's "short match": first to 9 (gamemode_competitive_short.cfg)
};

void PrepareOfficialMatch(const OfficialMatchSettings &settings);
void CancelOfficialMatch();

} // namespace MatchWatcher
