#pragma once

#include "gc_const.h"
#include "gc_shared.h"
#include "inventory.h"
#include "progress.h"

class GCMessageRead;

class CMsgClientWelcome;
class CMsgCStrike15Welcome;
class CMsgGCCStrike15_v2_MatchmakingGC2ClientHello;

struct AuthTicket
{
    uint64_t steamId{}; // gameserver
    std::vector<uint8_t> buffer;
};

class ClientGC final : public SharedGC
{
public:
    ClientGC(uint64_t steamId);
    ~ClientGC();

private:
    void HandleEvent(GCEvent type, uint64_t id, std::vector<uint8_t> &&buffer) override;

    // event handlers
    void HandleMessage(uint32_t type, const void *data, uint32_t size);
    void HandleNetMessage(uint64_t steamId, const void *data, uint32_t size);
    void SetAuthTicket(uint32_t handle, std::vector<uint8_t> &&buffer);
    void ClearAuthTicket(uint32_t handle);
    void HandleConnect(uint64_t steamId, GCMessageRead &message);
    void HandleSOCacheRequest();
    void InventoryUpdate();

    // send to either the local game or the game server we're connected to
    void SendMessageToGame(bool server, uint32_t type,
        const google::protobuf::MessageLite &message, uint64_t jobId = JobIdInvalid);

    void SendSOCacheMessages(const SOCacheMessages &messages, bool server);
    void SendInventoryChangeMessages(const InventoryChangeMessages &messages);

    void OnClientHello(GCMessageRead &messageRead);
    void OnMatchmakingHello();
    void OnRequestInventoryRefresh();
    void AdjustItemEquippedState(GCMessageRead &messageRead);
    void ClientPlayerDecalSign(GCMessageRead &messageRead);
    void UseItemRequest(GCMessageRead &messageRead);
    void ClientRequestJoinServerData(GCMessageRead &messageRead);
    void SetItemPositions(GCMessageRead &messageRead);
    void IncrementKillCountAttribute(GCMessageRead &messageRead);
    void ApplySticker(GCMessageRead &messageRead);
    void StoreGetUserData(GCMessageRead &messageRead);
    void StorePurchaseInit(GCMessageRead &messageRead);
    void StorePurchaseFinalize(GCMessageRead &messageRead);
    void StorePurchaseFinalizeStruct(GCMessageRead &messageRead);
    void OnGetEventFavorites(GCMessageRead &messageRead);
    void OnPartyRegister(GCMessageRead &messageRead);
    void RequestPrestigeCoin(GCMessageRead &messageRead);
    void StatTrakSwap(GCMessageRead &messageRead);
    void ModifyItemAttribute(GCMessageRead &messageRead);
    void RecurringSubscriptionStatus();
    void AdjustItemEquippedStateMulti(GCMessageRead &messageRead);
    void CommendPlayerQuery(GCMessageRead &messageRead);
    void CommendPlayer(GCMessageRead &messageRead);
    void MatchListRequestTournamentGames(GCMessageRead &messageRead);
    void MatchListRequestTournamentPredictions(GCMessageRead &messageRead);
    void RequestSouvenir(GCMessageRead &messageRead);

    void CraftItem(GCMessageRead &messageRead);
    void DeleteItem(GCMessageRead &messageRead);
    void UnlockCrate(GCMessageRead &messageRead);
    void NameItem(GCMessageRead &messageRead);
    void NameBaseItem(GCMessageRead &messageRead);
    void RemoveItemName(GCMessageRead &messageRead);

    void ProcessCasketItemLoadContents(GCMessageRead &messageRead);
    void ProcessCasketItemAdd(GCMessageRead &messageRead);
    void ProcessCasketItemExtract(GCMessageRead &messageRead);

    void BuildMatchmakingHello(CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &message);
    void BuildClientWelcome(CMsgClientWelcome &message, const CMsgCStrike15Welcome &csWelcome,
        const CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &matchmakingHello);
    void SendRankUpdate(RankType changedType = RankType{}, float rankChange = 0);
    void SendPlayerLevel();

    // a match against bots ended (rank_progress)
    void OnMatchResult(const MatchResult &result);

    // Official Matchmaking against bots (rank_progress): Go starts a search, a match is found
    // after a while, and accepting it in the popup starts a match against bots like Practice
    // with bots does (a ui patch). Matches set up this way are the ones that count
    void OnMatchmakingPreferences(std::string_view preferences);
    void MatchmakingStart(GCMessageRead &messageRead);
    void MatchmakingStop(GCMessageRead &messageRead);
    void MatchmakingTick();
    void SendMatchmakingStatus(int matchmaking, std::string_view error = {});
    void SetGameSetting(std::string_view name, std::string_view value);

    // the level or XP changed (service medal, bonus rank, a match), saves it with rank_progress
    void SetLevel(int level, int xp);

    struct RankInfo
    {
        uint32_t rankId;
        uint32_t wins;
    };

    RankInfo CompetitiveRank() const;
    RankInfo WingmanRank() const;
    RankInfo DangerZoneRank() const;

    uint32_t AccountId() const { return m_steamId & 0xffffffff; }

    const uint64_t m_steamId;

    Inventory m_inventory;

    // with rank_progress: level, XP and skill groups from matches against bots, saved
    PlayerProgress m_progress;

    // from config.txt, or the saved progress with rank_progress. without it claiming a service
    // medal or using a bonus rank only lasts for the session
    int m_level;
    int m_xp;

    // the play menu's settings sent with the last MatchmakingStart
    struct MatchmakingPreferences
    {
        std::string mode;
        std::string maps;
        std::string botDifficulty; // player_botdifflast_s, put back after a match
    };

    MatchmakingPreferences m_matchmakingPreferences;

    struct BotMatchSearch
    {
        bool searching{ false };
        std::chrono::steady_clock::time_point foundAt;
        RankedMode mode{ RankedMode::Competitive };
        std::vector<std::string> maps;
        uint32_t gameType{ 0 };
    };

    BotMatchSearch m_search;

    // a match found by matchmaking, counted when it ends
    struct OfficialMatch
    {
        bool pending{ false };
        RankedMode mode{ RankedMode::Competitive };
        std::string map;
        int botDifficulty{ 0 };
        std::chrono::steady_clock::time_point expires;
        std::string restoreBotDifficulty;
    };

    OfficialMatch m_officialMatch;

    // commendations given this session by account id, so the commend popup shows them again
    std::unordered_map<uint32_t, PlayerCommendationInfo> m_commendations;

    uint64_t m_serverSteamId{};
    std::unordered_map<uint32_t, AuthTicket> m_tickets;

    // microtransactions, we only have one going at a time
    uint64_t m_transactionId{};
    std::vector<uint64_t> m_transactionItemIds;
};
