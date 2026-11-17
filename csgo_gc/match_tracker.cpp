#include "stdafx.h"
#include "match_tracker.h"
#include "bot_profiles.h"
#include "config.h"
#include "gc_shared.h"

#include <mutex>

constexpr int TeamTerrorist = 2;
constexpr int TeamCounterTerrorist = 3;

static const char *ModeName(RankedMode mode)
{
    return mode == RankedMode::Wingman ? "wingman" : "competitive";
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
            Platform::Print("Ranked {} match started\n", ModeName(m_mode));
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

// IBaseClientDLL::DispatchUserMessage is virtual 38 in the final client.dll (VClient018), it's
// what the engine hands the server's user messages to
using DispatchUserMessage_t = bool(__thiscall *)(void *client, int type, int32_t flags, int size, const void *data);

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

    // set from the server's events (under the mutex), cleared for every new server
    bool humanConnected{};
    bool humanOnTeam{};

    // a local server started or stopped (under the mutex). the listener registered at the main
    // menu stays registered into the next map, so the server's own events are what tell
    bool serverSpawned{};
    bool serverShutDown{};

    EngineEventManager *events{};
    void *cvar{};
    void *engineClient{};
    bool triedInterfaces{};
    bool listening{};

    // counts local servers: a new one starts when the engine resets its events, or the server
    // says it spawned
    uint32_t serverGeneration{};
    std::chrono::steady_clock::time_point serverStarted;

    // Official Matchmaking against bots, main thread only
    struct OfficialMatch
    {
        enum class Stage
        {
            None,
            WaitingForServer, // for the player to be in the next local server
            Joining, // official settings applied, getting the player on their team
            Playing,
        };

        Stage stage{ Stage::None };
        uint32_t afterGeneration{};
        MatchWatcher::OfficialMatchSettings settings;
        int joinAttempts{};
        std::chrono::steady_clock::time_point nextStep;
    };

    OfficialMatch official;
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

    // bots never send player_connect_full, the person playing does once they're in
    const std::string_view name = matchEvent.Name();
    if (name == "server_spawn" || name == "game_newmap")
    {
        // nobody's in the new server yet, the player connects after this
        state.serverSpawned = true;
        state.humanConnected = false;
        state.humanOnTeam = false;
    }
    else if (name == "server_shutdown")
    {
        state.serverShutDown = true;
    }
    else if (name == "player_connect_full")
    {
        state.humanConnected = true;
    }
    else if (name == "player_team" && !matchEvent.GetBool("isbot") && matchEvent.GetInt("team") >= 2)
    {
        state.humanOnTeam = true;
    }
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
static void RunConsoleCommand(const char *command);

constexpr const char *ReservationConVar = "sv_mmqueue_reservation";

static void OnServerGone(WatcherState &state)
{
    {
        std::lock_guard lock{ state.mutex };
        PostResult(state, state.tracker.OnServerGone());
    }

    // the official match is over, the reservation would make the next local server official too
    using Stage = WatcherState::OfficialMatch::Stage;
    if (state.official.stage == Stage::Joining || state.official.stage == Stage::Playing)
    {
        state.official.stage = Stage::None;
        MatchWatcher::SetGameSetting(ReservationConVar, "");
    }
}

// resetPlayers is false when the server's spawn event already did that: the player may have
// connected since
static void OnNewServer(WatcherState &state, bool resetPlayers = true)
{
    state.serverGeneration++;
    state.serverStarted = std::chrono::steady_clock::now();

    if (resetPlayers)
    {
        std::lock_guard lock{ state.mutex };
        state.humanConnected = false;
        state.humanOnTeam = false;
    }

    // a server nobody set up as official mustn't be one
    if (state.official.stage != WatcherState::OfficialMatch::Stage::WaitingForServer
        && !MatchWatcher::GetGameSetting(ReservationConVar).empty())
    {
        MatchWatcher::SetGameSetting(ReservationConVar, "");
    }
}

// server.dll's code and data, empty when it isn't loaded
static std::string_view ServerImage()
{
    const auto base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(L"server.dll"));
    if (!base)
    {
        return {};
    }

    const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dosHeader->e_lfanew);
    return { reinterpret_cast<const char *>(base), ntHeaders->OptionalHeader.SizeOfImage };
}

// official servers only had bots in the places of players who left, and always easy ones:
// server.dll's pick of a bot's difficulty (for bots joining and the difficulty adjustment) is
// easy whenever sv_mmqueue_reservation is set. csgo_gc's official matches are played against
// bots, so they use bot_difficulty like any other server: the check of the reservation's 'Q'
// jumps past whatever it says. Only servers on this machine run this server.dll, and only
// csgo_gc makes them official
static bool UseBotDifficultyInOfficialMatches()
{
    static bool patched;
    if (patched)
    {
        return true;
    }

    // cmp byte ptr [eax], 'Q'; jne <bot_difficulty>; cmp dword ptr [<game rules>], 0; je ...;
    // mov ecx, [<game types>]; mov eax, [ecx]; call [eax + 0x20]; cmp eax, 4; je ...; xor eax, eax
    constexpr std::string_view Check{ "\x80\x38\x51\x75\x1f\x83\x3d", 7 };
    constexpr std::string_view After{ "\x8b\x01\xff\x50\x20\x83\xf8\x04\x74\x06\x33\xc0", 12 };
    const std::string_view image = ServerImage();
    const size_t check = image.find(Check);
    if (check == std::string_view::npos || image.substr(check + 20, After.size()) != After)
    {
        return false;
    }

    // jne -> jmp
    auto jump = const_cast<char *>(image.data() + check + 3);
    DWORD protection;
    if (!VirtualProtect(jump, 1, PAGE_EXECUTE_READWRITE, &protection))
    {
        return false;
    }

    *jump = '\xeb';
    VirtualProtect(jump, 1, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), jump, 1);

    patched = true;
    return true;
}

// server.dll's bot manager loads the bot profiles whenever a map starts, from BotProfile.db (when
// no BotPackList.db lists others). An official match's map loads csgo_gc's bots for the player's
// skill group instead: the name it pushes points at csgo_gc's file for that one map
class ProfileDatabaseSwitch
{
public:
    // writes the tier's bots and points the next map's bot manager at them
    bool Use(RankId tier)
    {
        uint32_t *operand = Operand();
        if (!operand)
        {
            return false;
        }

        char path[MAX_PATH];
        if (!GetFullPathNameA("csgo_gc\\bot_profiles.db", sizeof(path), path, nullptr))
        {
            return false;
        }

        FILE *f = fopen(path, "wb");
        if (!f)
        {
            return false;
        }

        const std::string database = BotProfiles::Database(tier);
        fwrite(database.data(), 1, database.size(), f);
        fclose(f);

        m_path = path;
        Write(operand, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_path.c_str())));
        return true;
    }

    // Valve's bots again, for the maps after
    void Restore()
    {
        if (m_operand && m_original)
        {
            Write(m_operand, m_original);
        }
    }

private:
    // the operand of push "BotProfile.db" in the bot manager (push ecx; push <name>; call)
    uint32_t *Operand()
    {
        if (m_operand)
        {
            return m_operand;
        }

        const std::string_view image = ServerImage();
        const size_t name = image.find(std::string_view{ "BotProfile.db", sizeof("BotProfile.db") });
        if (name == std::string_view::npos)
        {
            return nullptr;
        }

        const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image.data()) + name);
        char push[7] = { '\x51', '\x68', 0, 0, 0, 0, '\xe8' };
        memcpy(push + 2, &address, sizeof(address));
        const size_t use = image.find(std::string_view{ push, sizeof(push) });
        if (use == std::string_view::npos)
        {
            return nullptr;
        }

        m_operand = reinterpret_cast<uint32_t *>(const_cast<char *>(image.data() + use + 2));
        m_original = address;
        return m_operand;
    }

    static void Write(uint32_t *operand, uint32_t value)
    {
        DWORD protection;
        if (VirtualProtect(operand, sizeof(*operand), PAGE_EXECUTE_READWRITE, &protection))
        {
            *operand = value;
            VirtualProtect(operand, sizeof(*operand), protection, &protection);
            FlushInstructionCache(GetCurrentProcess(), operand, sizeof(*operand));
        }
    }

    uint32_t *m_operand{};
    uint32_t m_original{};
    std::string m_path; // the code points at it while switched
};

static ProfileDatabaseSwitch &ProfileDatabase()
{
    static ProfileDatabaseSwitch *profiles = new ProfileDatabaseSwitch; // never freed, like the state
    return *profiles;
}

// what official servers did with a reservation, worked out with dev_commands and server.dll: the
// reservation set as soon as the server's up, before the player's in (set before the map loads,
// the server waits for the reserved players forever), the competitive config's own bot kicked so
// every bot gets the difficulty matchmaking picked, then joingame, which the server answers by
// putting the player on their reserved team
static void UpdateOfficialMatch(WatcherState &state)
{
    using Stage = WatcherState::OfficialMatch::Stage;
    WatcherState::OfficialMatch &match = state.official;

    if (!state.listening || match.stage == Stage::None || match.stage == Stage::Playing
        || state.serverGeneration <= match.afterGeneration)
    {
        return;
    }

    bool connected;
    bool onTeam;
    {
        std::lock_guard lock{ state.mutex };
        connected = state.humanConnected;
        onTeam = state.humanOnTeam;
    }

    const auto now = std::chrono::steady_clock::now();

    if (match.stage == Stage::WaitingForServer)
    {
        // before the reservation makes the bots joining below easy ones
        if (!UseBotDifficultyInOfficialMatches())
        {
            Platform::Print("Can't give official matches' bots their difficulty in this server.dll, they'll be easy\n");
        }

        const std::string warmup = MatchWatcher::GetGameSetting("mp_warmuptime_all_players_connected");
        MatchWatcher::SetGameSetting(ReservationConVar, match.settings.reservation.c_str());
        // the map loaded csgo_gc's bots for the player's skill group, all of them expert ones. maps
        // after this one get Valve's again
        ProfileDatabase().Restore();
        MatchWatcher::SetGameSetting("bot_difficulty", "3");
        MatchWatcher::SetGameSetting("sv_auto_adjust_bot_difficulty", "0");
        RunConsoleCommand("bot_kick");
        MatchWatcher::SetGameSetting("bot_quota_mode", "fill");
        MatchWatcher::SetGameSetting("bot_quota", std::to_string(match.settings.players).c_str());

        // the bots count as connected, so the short warmup official servers switched to then
        MatchWatcher::SetGameSetting("mp_warmuptime", warmup.empty() || warmup == "0" ? "60" : warmup.c_str());

        if (match.settings.shortMatch)
        {
            MatchWatcher::SetGameSetting("mp_maxrounds", "16");
            MatchWatcher::SetGameSetting("mp_starting_losses", "2");
        }

        Platform::Print("Running this match as an official matchmaking server: {}\n", match.settings.reservation);

        match.stage = Stage::Joining;
        match.joinAttempts = 0;
        match.nextStep = {};

        std::lock_guard lock{ state.mutex };
        if (state.gc)
        {
            state.gc->PostToGC(GCEvent::OfficialMatchStarted, 0, nullptr, 0);
        }

        return;
    }

    // Stage::Joining. a player who sends joingame with the reservation in place goes straight onto
    // their reserved team, without the team menu. the client only sends it by itself when
    // matchmaking connected it, so send it as soon as the player's in, and keep at it until the
    // server takes it (it ignores joingame until the player's at the welcome stage)
    if (onTeam)
    {
        match.stage = Stage::Playing;
        return;
    }

    if (!connected || now < match.nextStep)
    {
        return;
    }

    constexpr int MaxJoinAttempts = 60; // 15 seconds
    if (match.joinAttempts++ >= MaxJoinAttempts)
    {
        Platform::Print("Couldn't get onto a team in the official match\n");
        match.stage = Stage::Playing;
        return;
    }

    RunConsoleCommand("joingame");
    match.nextStep = now + std::chrono::milliseconds{ 250 };
}

void MatchWatcher::PrepareOfficialMatch(const OfficialMatchSettings &settings)
{
    WatcherState &state = State();
    state.official.stage = WatcherState::OfficialMatch::Stage::WaitingForServer;
    state.official.afterGeneration = state.serverGeneration;
    state.official.settings = settings;

    // the map accepting the match starts loads bots of the player's skill group
    if (!ProfileDatabase().Use(settings.botTier))
    {
        Platform::Print("Can't give the match bots of your skill group in this server.dll, it'll have Valve's\n");
    }
}

void MatchWatcher::DispatchUserMessage(int type, const void *data, int size)
{
    static void *client;
    if (!client)
    {
        HMODULE module = GetModuleHandleW(L"client.dll");
        auto factory = module ? reinterpret_cast<CreateInterface_t>(GetProcAddress(module, "CreateInterface")) : nullptr;
        client = factory ? factory("VClient018", nullptr) : nullptr;
        if (!client)
        {
            Platform::Print("Can't get the client to show the end of match screens\n");
            return;
        }
    }

    // the client reports unknown and malformed messages in the console itself
    (*reinterpret_cast<DispatchUserMessage_t **>(client))[38](client, type, 0, size, data);
}

void MatchWatcher::CancelOfficialMatch()
{
    WatcherState &state = State();
    if (state.official.stage == WatcherState::OfficialMatch::Stage::WaitingForServer)
    {
        state.official.stage = WatcherState::OfficialMatch::Stage::None;
        ProfileDatabase().Restore();
    }
}

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
    if (!state.events->FindListener(&Listener(), "round_end"))
    {
        if (state.listening)
        {
            state.listening = false;
            OnServerGone(state);
        }

        state.events->AddListenerGlobal(&Listener(), true);
        state.listening = state.events->FindListener(&Listener(), "round_end");
        if (state.listening)
        {
            OnNewServer(state);
        }
    }

    bool shutDown;
    bool spawned;
    {
        std::lock_guard lock{ state.mutex };
        shutDown = std::exchange(state.serverShutDown, false);
        spawned = std::exchange(state.serverSpawned, false);
    }

    // a map change is both, the old server first
    if (shutDown)
    {
        OnServerGone(state);
    }

    if (spawned)
    {
        OnNewServer(state, false);
    }

    UpdateOfficialMatch(state);
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

static void RunConsoleCommand(const char *command)
{
    WatcherState &state = State();
    if (!state.engineClient && !GetInterfaces(state))
    {
        return;
    }

    if (state.engineClient)
    {
        (*reinterpret_cast<ExecuteClientCmd_t **>(state.engineClient))[108](state.engineClient, command);
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
            Platform::Print("dev: running \"{}\"\n", argument);
            RunConsoleCommand(argument.c_str());
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
        else if (verb == "watcher")
        {
            WatcherState &state = State();
            std::lock_guard lock{ state.mutex };
            Platform::Print("dev: listening {}, server generation {}, official stage {} after generation {}, human connected {}, on team {}, bot tier {}\n",
                state.listening, state.serverGeneration, FromEnum(state.official.stage), state.official.afterGeneration,
                state.humanConnected, state.humanOnTeam, FromEnum(state.official.settings.botTier));
        }
        else
        {
            Platform::Print("dev: unknown command \"{}\"\n", line);
        }
    }
}

#else

void MatchWatcher::PrepareOfficialMatch(const OfficialMatchSettings &)
{
}

void MatchWatcher::CancelOfficialMatch()
{
}

void MatchWatcher::DispatchUserMessage(int, const void *, int)
{
}

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
