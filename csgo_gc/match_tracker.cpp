#include "stdafx.h"
#include "match_tracker.h"
#include "config.h"
#include "gc_shared.h"

#include <mutex>

constexpr int TeamTerrorist = 2;
constexpr int TeamCounterTerrorist = 3;

static const char *ModeName(RankedMode mode)
{
    return mode == RankedMode::Wingman ? "wingman" : "competitive";
}

static const char *BotDifficultyName(int difficulty)
{
    constexpr const char *Names[] = { "easy", "normal", "hard", "expert" };
    return Names[std::clamp(difficulty, 0, 3)];
}

std::optional<RankedMode> MatchTracker::RankedModeFor(const MatchSettings &settings)
{
    // game_type 0 is classic: game_mode 1 competitive, 2 wingman
    if (settings.gameType == 0 && settings.gameMode == 1)
    {
        return RankedMode::Competitive;
    }

    if (settings.gameType == 0 && settings.gameMode == 2)
    {
        return RankedMode::Wingman;
    }

    return std::nullopt;
}

std::optional<MatchResult> MatchTracker::OnEvent(const MatchEvent &event, const std::function<MatchSettings()> &readSettings)
{
    const std::string_view name = event.Name();

    // who's who. bots never send player_connect_full, people do once they're in
    if (name == "player_connect")
    {
        Player &player = m_players[event.GetInt("userid")];
        player.bot = event.GetString("networkid") == "BOT";
        player.known = true;
        return std::nullopt;
    }

    if (name == "player_connect_full")
    {
        Player &player = m_players[event.GetInt("userid")];
        player.bot = false;
        player.known = true;
        return std::nullopt;
    }

    if (name == "player_team")
    {
        Player &player = m_players[event.GetInt("userid")];
        player.team = event.GetInt("team");
        player.bot = event.GetBool("isbot");
        player.known = true;
        return std::nullopt;
    }

    if (name == "player_spawn")
    {
        // the team someone plays a round on, also right after the sides switch at halftime
        m_players[event.GetInt("userid")].team = event.GetInt("teamnum");
        return std::nullopt;
    }

    if (name == "player_disconnect")
    {
        const int userId = event.GetInt("userid");
        const std::optional<int> human = HumanUserId();
        m_players.erase(userId);

        // leaving a match is abandoning it
        if (m_live && human == userId)
        {
            return Finish(true);
        }

        return std::nullopt;
    }

    if (name == "begin_new_match")
    {
        // warmup is over (or the match restarted), count from here
        m_settings = readSettings();
        const std::optional<RankedMode> mode = RankedModeFor(m_settings);
        m_live = mode.has_value();
        m_mode = mode.value_or(RankedMode::Competitive);
        m_roundsWon = m_roundsLost = m_kills = m_deaths = m_mvps = 0;

        if (m_live)
        {
            Platform::Print("Ranked {} match against {} bots started\n", ModeName(m_mode), BotDifficultyName(m_settings.botDifficulty));
        }

        return std::nullopt;
    }

    if (!m_live)
    {
        return std::nullopt;
    }

    const std::optional<int> human = HumanUserId();

    if (name == "round_end")
    {
        const int winner = event.GetInt("winner");
        if (!human || (winner != TeamTerrorist && winner != TeamCounterTerrorist))
        {
            return std::nullopt;
        }

        const int team = m_players[*human].team;
        if (team == winner)
        {
            m_roundsWon++;
        }
        else if (team == TeamTerrorist || team == TeamCounterTerrorist)
        {
            m_roundsLost++;
        }
    }
    else if (name == "player_death" && human)
    {
        const int victim = event.GetInt("userid");
        if (event.GetInt("attacker") == *human && victim != *human)
        {
            m_kills++;
        }

        if (victim == *human)
        {
            m_deaths++;
        }
    }
    else if (name == "round_mvp" && human)
    {
        if (event.GetInt("userid") == *human)
        {
            m_mvps++;
        }
    }
    else if (name == "cs_win_panel_match")
    {
        return Finish(false);
    }

    return std::nullopt;
}

std::optional<MatchResult> MatchTracker::OnServerGone()
{
    std::optional<MatchResult> result = Finish(true);
    m_players.clear();
    return result;
}

std::optional<int> MatchTracker::HumanUserId() const
{
    std::optional<int> human;
    for (const auto &[userId, player] : m_players)
    {
        if (player.known && !player.bot)
        {
            if (human)
            {
                return std::nullopt; // someone else is playing too
            }

            human = userId;
        }
    }

    return human;
}

std::optional<MatchResult> MatchTracker::Finish(bool abandoned)
{
    if (!m_live)
    {
        return std::nullopt;
    }

    m_live = false;

    if (!HumanUserId() && !abandoned)
    {
        Platform::Print("Not counting the match, it wasn't just you against bots\n");
        return std::nullopt;
    }

    MatchResult result;
    result.mode = m_mode;
    result.botDifficulty = std::clamp(m_settings.botDifficulty, 0, 3);
    result.abandoned = abandoned;
    result.roundsWon = m_roundsWon;
    result.roundsLost = m_roundsLost;
    result.kills = m_kills;
    result.deaths = m_deaths;
    result.mvps = m_mvps;

    if (abandoned || m_roundsWon < m_roundsLost)
    {
        result.outcome = MatchOutcome::Loss;
    }
    else if (m_roundsWon > m_roundsLost)
    {
        result.outcome = MatchOutcome::Win;
    }
    else
    {
        result.outcome = MatchOutcome::Tie;
    }

    return result;
}

#ifdef _WIN32

#include <windows.h>

namespace
{

// just enough of the engine's interfaces (engine.dll and vstdlib.dll of the final build)

class EngineGameEvent
{
public:
    virtual void Destructor() = 0; // 0
    virtual const char *GetName() const = 0; // 1
    virtual bool IsReliable() const = 0; // 2
    virtual bool IsLocal() const = 0; // 3
    virtual bool IsEmpty(const char *key) = 0; // 4
    virtual bool GetBool(const char *key, bool fallback) = 0; // 5
    virtual int GetInt(const char *key, int fallback) = 0; // 6
    virtual uint64_t GetUint64(const char *key, uint64_t fallback) = 0; // 7
    virtual float GetFloat(const char *key, float fallback) = 0; // 8
    virtual const char *GetString(const char *key, const char *fallback) = 0; // 9
};

class EventListener
{
public:
    virtual void Destructor() {} // 0, the engine never deletes listeners
    virtual void FireGameEvent(EngineGameEvent *event); // 1
    virtual int GetEventDebugID() { return 42; } // 2, EVENT_DEBUG_ID_INIT
};

class EngineEventManager
{
public:
    virtual void Destructor() = 0; // 0
    virtual int LoadEventsFromFile(const char *fileName) = 0; // 1
    virtual void Reset() = 0; // 2
    virtual bool AddListener(EventListener *listener, const char *name, bool serverSide) = 0; // 3
    virtual bool FindListener(EventListener *listener, const char *name) = 0; // 4
    virtual void RemoveListener(EventListener *listener) = 0; // 5
    virtual bool AddListenerGlobal(EventListener *listener, bool serverSide) = 0; // 6
};

using CreateInterface_t = void *(__cdecl *)(const char *name, int *returnCode);

// IVEngineClient::ExecuteClientCmd is virtual 108, it runs the command right away
using ExecuteClientCmd_t = void(__thiscall *)(void *engineClient, const char *command);

// ICvar::FindVar is virtual 16, ConVar::GetInt and SetValue(const char *) are 13 and 14
using FindVar_t = void *(__thiscall *)(void *cvar, const char *name);
using ConVarGetInt_t = int(__thiscall *)(void *conVar);
using ConVarSetString_t = void(__thiscall *)(void *conVar, const char *value);

// ConVar::m_pParent, and the string value in its CVValue_t (GetInt reads m_nValue at 0x30)
constexpr size_t ConVarParentOffset = 0x1c;
constexpr size_t ConVarStringOffset = 0x24;

class EngineMatchEvent final : public MatchEvent
{
public:
    EngineMatchEvent(EngineGameEvent &event)
        : m_event{ event }
    {
    }

    std::string_view Name() const override { return m_event.GetName(); }
    int GetInt(const char *key) const override { return m_event.GetInt(key, 0); }
    bool GetBool(const char *key) const override { return m_event.GetBool(key, false); }
    std::string_view GetString(const char *key) const override { return m_event.GetString(key, ""); }

private:
    EngineGameEvent &m_event;
};

struct WatcherState
{
    // guards the tracker and gc. never held while calling into the engine, which fires events
    // with its own lock held
    std::mutex mutex;
    MatchTracker tracker;
    SharedGC *gc{};

    EngineEventManager *events{};
    void *cvar{};
    void *engineClient{};
    bool triedInterfaces{};
    bool listening{};
};

// never freed, the engine could still have the listener during shutdown
WatcherState &State()
{
    static WatcherState *state = new WatcherState;
    return *state;
}

EventListener &Listener()
{
    static EventListener *listener = new EventListener;
    return *listener;
}

int ConVarInt(void *cvar, const char *name, int fallback)
{
    void *conVar = (*reinterpret_cast<FindVar_t **>(cvar))[16](cvar, name);
    if (!conVar)
    {
        return fallback;
    }

    return (*reinterpret_cast<ConVarGetInt_t **>(conVar))[13](conVar);
}

void PostResult(WatcherState &state, const std::optional<MatchResult> &result)
{
    if (result && state.gc)
    {
        static_assert(std::is_trivially_copyable_v<MatchResult>);
        state.gc->PostToGC(GCEvent::MatchResult, 0, &*result, sizeof(*result));
    }
}

void EventListener::FireGameEvent(EngineGameEvent *event)
{
    if (!event)
    {
        return;
    }

    WatcherState &state = State();
    EngineMatchEvent matchEvent{ *event };

    auto readSettings = [&state]
    {
        MatchSettings settings;
        settings.gameType = ConVarInt(state.cvar, "game_type", -1);
        settings.gameMode = ConVarInt(state.cvar, "game_mode", -1);
        settings.botDifficulty = ConVarInt(state.cvar, "bot_difficulty", 0);
        return settings;
    };

    std::lock_guard lock{ state.mutex };
    PostResult(state, state.tracker.OnEvent(matchEvent, readSettings));
}

bool GetInterfaces(WatcherState &state)
{
    HMODULE engine = GetModuleHandleW(L"engine.dll");
    HMODULE vstdlib = GetModuleHandleW(L"vstdlib.dll");
    if (!engine || !vstdlib)
    {
        return false;
    }

    auto engineFactory = reinterpret_cast<CreateInterface_t>(GetProcAddress(engine, "CreateInterface"));
    auto vstdlibFactory = reinterpret_cast<CreateInterface_t>(GetProcAddress(vstdlib, "CreateInterface"));
    if (!engineFactory || !vstdlibFactory)
    {
        return false;
    }

    state.events = static_cast<EngineEventManager *>(engineFactory("GAMEEVENTSMANAGER002", nullptr));
    state.cvar = vstdlibFactory("VEngineCvar007", nullptr);
    state.engineClient = engineFactory("VEngineClient014", nullptr);
    return state.events && state.cvar;
}

} // namespace

static void RunDevCommands();

void MatchWatcher::Update(SharedGC &gc)
{
    if (GetConfig().m_devCommands)
    {
        RunDevCommands();
    }

    if (!GetConfig().m_rankProgress)
    {
        return;
    }

    WatcherState &state = State();
    {
        std::lock_guard lock{ state.mutex };
        state.gc = &gc;
    }

    if (!state.events)
    {
        if (state.triedInterfaces)
        {
            return;
        }

        if (!GetInterfaces(state))
        {
            // engine.dll isn't loaded yet, or a different game version
            if (GetModuleHandleW(L"engine.dll"))
            {
                state.triedInterfaces = true;
                Platform::Print("Can't get the engine's game events, matches against bots won't count\n");
            }

            return;
        }
    }

    // the engine drops every listener when a server starts or stops, so register again whenever
    // we're gone. with no server running there are no events and this does nothing
    if (state.events->FindListener(&Listener(), "round_end"))
    {
        return;
    }

    if (state.listening)
    {
        state.listening = false;

        std::lock_guard lock{ state.mutex };
        PostResult(state, state.tracker.OnServerGone());
    }

    state.events->AddListenerGlobal(&Listener(), true);
    state.listening = state.events->FindListener(&Listener(), "round_end");
}

void MatchWatcher::Shutdown()
{
    WatcherState &state = State();
    if (state.events && state.listening)
    {
        state.events->RemoveListener(&Listener());
        state.listening = false;
    }

    std::lock_guard lock{ state.mutex };
    state.gc = nullptr;
}

static void *FindConVar(const char *name)
{
    WatcherState &state = State();
    if (!state.cvar && !GetInterfaces(state))
    {
        return nullptr;
    }

    return (*reinterpret_cast<FindVar_t **>(state.cvar))[16](state.cvar, name);
}

std::string MatchWatcher::GetGameSetting(const char *name)
{
    void *conVar = FindConVar(name);
    if (!conVar)
    {
        return {};
    }

    void *parent = *reinterpret_cast<void **>(static_cast<char *>(conVar) + ConVarParentOffset);
    const char *value = *reinterpret_cast<const char **>(static_cast<char *>(parent ? parent : conVar) + ConVarStringOffset);
    return value ? value : "";
}

void MatchWatcher::SetGameSetting(const char *name, const char *value)
{
    void *conVar = FindConVar(name);
    if (conVar)
    {
        (*reinterpret_cast<ConVarSetString_t **>(conVar))[14](conVar, value);
    }
}

static void RunDevCommands()
{
    constexpr const char *Path = "csgo_gc/dev_commands.txt";

    static auto nextCheck = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now < nextCheck)
    {
        return;
    }

    nextCheck = now + std::chrono::milliseconds{ 500 };

    FILE *f = fopen(Path, "rb");
    if (!f)
    {
        return;
    }

    std::string text;
    char buffer[4096];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), f)) > 0)
    {
        text.append(buffer, read);
    }

    fclose(f);
    Platform::RemoveFile(Path);

    std::string_view rest = text;
    while (!rest.empty())
    {
        const size_t end = rest.find('\n');
        std::string line{ rest.substr(0, end) };
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);

        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        {
            line.pop_back();
        }

        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        const size_t space = line.find(' ');
        const std::string verb = line.substr(0, space);
        const std::string argument = space == std::string::npos ? std::string{} : line.substr(space + 1);

        if (verb == "cmd")
        {
            WatcherState &state = State();
            if (state.engineClient)
            {
                Platform::Print("dev: running \"{}\"\n", argument);
                (*reinterpret_cast<ExecuteClientCmd_t **>(state.engineClient))[108](state.engineClient, argument.c_str());
            }
        }
        else if (verb == "set" || verb == "get")
        {
            const size_t nameEnd = argument.find(' ');
            const std::string name = argument.substr(0, nameEnd);
            if (verb == "set")
            {
                MatchWatcher::SetGameSetting(name.c_str(), nameEnd == std::string::npos ? "" : argument.c_str() + nameEnd + 1);
            }

            Platform::Print("dev: {} = \"{}\"{}\n", name, MatchWatcher::GetGameSetting(name.c_str()),
                FindConVar(name.c_str()) ? "" : " (no such convar)");
        }
        else
        {
            Platform::Print("dev: unknown command \"{}\"\n", line);
        }
    }
}

#else

std::string MatchWatcher::GetGameSetting(const char *)
{
    return {};
}

void MatchWatcher::SetGameSetting(const char *, const char *)
{
}

void MatchWatcher::Update(SharedGC &)
{
    // FIXME: same thing with engine_client.so/dylib
}

void MatchWatcher::Shutdown()
{
}

#endif
