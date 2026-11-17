#include "stdafx.h"
#include "gc_client.h"
#include "bot_matchmaking.h"
#include "bot_profiles.h"
#include "config.h"
#include "end_of_match.h"
#include "gc_message.h"
#include "graffiti.h"
#include "inventory_messages.h"
#include "keyvalue.h"
#include "networking_shared.h"
#include "random.h"
#include "tournament.h"

#include "base_gcmessages.pb.h"
#include "cstrike15_gcmessages.pb.h"
#include "cstrike15_usermessages.pb.h"
#include "econ_gcmessages.pb.h"
#include "gcsdk_gcmessages.pb.h"
#include "gcsystemmsgs.pb.h"

// FIXME: add to protos?
constexpr int k_EMsgGCRequestInventoryRefresh = 1050;

// the client offers a service medal at this level (GetMaxLevel in client.dll)
constexpr int MaxPlayerLevel = 40;

ClientGC::ClientGC(uint64_t steamId)
    : m_steamId{ steamId }
    , m_inventory{ steamId }
    , m_level{ GetConfig().m_level }
    , m_xp{ GetConfig().m_xp }
{
    // also called from ServerGC's constructor
    Graffiti::Initialize();

    if (GetConfig().m_rankProgress)
    {
        m_progress.Load();
        m_level = m_progress.Level();
        m_xp = m_progress.Xp();
    }

    StartThread();

    Platform::Print("ClientGC spawned for user {}\n", steamId);
}

ClientGC::~ClientGC()
{
    StopThread();
    Platform::Print("ClientGC destroyed\n");
}

void ClientGC::HandleEvent(GCEvent type, uint64_t id, std::vector<uint8_t> &&buffer)
{
    switch (type)
    {
    case GCEvent::Message:
        HandleMessage(static_cast<uint32_t>(id), buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;

    case GCEvent::NetMessage:
        HandleNetMessage(id, buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;

    case GCEvent::SetAuthTicket:
        SetAuthTicket(static_cast<uint32_t>(id), std::move(buffer));
        break;

    case GCEvent::ClearAuthTicket:
        ClearAuthTicket(static_cast<uint32_t>(id));
        break;

    case GCEvent::Tick:
        InventoryUpdate();
        MatchmakingTick();
        break;

    case GCEvent::MatchResult:
        if (buffer.size() == sizeof(MatchResult))
        {
            MatchResult result;
            memcpy(&result, buffer.data(), sizeof(result));
            OnMatchResult(result);
        }
        break;

    case GCEvent::MatchmakingPreferences:
        OnMatchmakingPreferences({ reinterpret_cast<const char *>(buffer.data()), buffer.size() });
        break;

    case GCEvent::OfficialMatchStarted:
        m_officialMatch.started = true;
        m_reservedServer.Close();
        break;

    default:
        assert(false);
        break;
    }
}

void ClientGC::HandleMessage(uint32_t type, const void *data, uint32_t size)
{
    GCMessageRead messageRead{ type, data, size };
    if (!messageRead.IsValid())
    {
        assert(false);
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCClientHello:
        case k_EMsgGCClientHelloPW:
        case k_EMsgGCClientHelloR2:
        case k_EMsgGCClientHelloR3:
        case k_EMsgGCClientHelloR4:
        case k_EMsgGCClientHelloPartner:
            OnClientHello(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_MatchmakingClient2GCHello:
            OnMatchmakingHello();
            break;

        case k_EMsgGCCStrike15_v2_MatchmakingStart:
            MatchmakingStart(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_MatchmakingStop:
            MatchmakingStop(messageRead);
            break;

        case k_EMsgGCRequestInventoryRefresh:
            OnRequestInventoryRefresh();
            break;

        case k_EMsgGCAdjustItemEquippedState:
            AdjustItemEquippedState(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientPlayerDecalSign:
            ClientPlayerDecalSign(messageRead);
            break;

        case k_EMsgGCUseItemRequest:
            UseItemRequest(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientRequestJoinServerData:
            ClientRequestJoinServerData(messageRead);
            break;

        case k_EMsgGCSetItemPositions:
            SetItemPositions(messageRead);
            break;

        case k_EMsgGCApplySticker:
            ApplySticker(messageRead);
            break;

        case k_EMsgGCStoreGetUserData:
            StoreGetUserData(messageRead);
            break;

        case k_EMsgGCStorePurchaseInit:
            StorePurchaseInit(messageRead);
            break;

        case k_EMsgGCStorePurchaseFinalize:
            StorePurchaseFinalize(messageRead);
            break;

        case k_EMsgGCCasketItemLoadContents:
            ProcessCasketItemLoadContents(messageRead);
            break;

        case k_EMsgGCCasketItemAdd:
            ProcessCasketItemAdd(messageRead);
            break;

        case k_EMsgGCCasketItemExtract:
            ProcessCasketItemExtract(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_GetEventFavorites_Request:
            OnGetEventFavorites(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_Party_Register:
            OnPartyRegister(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin:
            RequestPrestigeCoin(messageRead);
            break;

        case k_EMsgGCStatTrakSwap:
            StatTrakSwap(messageRead);
            break;

        case k_EMsgGCModifyItemAttribute:
            ModifyItemAttribute(messageRead);
            break;

        case k_EMsgGCRecurringSubscriptionStatus:
            RecurringSubscriptionStatus();
            break;

        case k_EMsgGCAdjustItemEquippedStateMulti:
            AdjustItemEquippedStateMulti(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientCommendPlayerQuery:
            CommendPlayerQuery(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientCommendPlayer:
            CommendPlayer(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames:
            MatchListRequestTournamentGames(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions:
            MatchListRequestTournamentPredictions(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientRequestSouvenir:
            RequestSouvenir(messageRead);
            break;

        default:
            Platform::Print("ClientGC::HandleMessage: unhandled protobuf message {}\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
    else
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCCraft:
            CraftItem(messageRead);
            break;

        case k_EMsgGCDelete:
            DeleteItem(messageRead);
            break;

        case k_EMsgGCUnlockCrate:
            UnlockCrate(messageRead);
            break;

        case k_EMsgGCNameItem:
            NameItem(messageRead);
            break;

        case k_EMsgGCNameBaseItem:
            NameBaseItem(messageRead);
            break;

        case k_EMsgGCRemoveItemName:
            RemoveItemName(messageRead);
            break;

        case k_EMsgGCStorePurchaseFinalize:
            StorePurchaseFinalizeStruct(messageRead);
            break;

        default:
            Platform::Print("ClientGC::HandleMessage: unhandled struct message {}\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
}

void ClientGC::HandleNetMessage(uint64_t steamId, const void *data, uint32_t size)
{
    // pass 0 as type so it gets parsed from the message
    GCMessageRead messageRead{ 0, data, size };
    if (!messageRead.IsValid())
    {
        assert(false);
        return;
    }

    if (!messageRead.IsProtobuf() && messageRead.TypeUnmasked() == k_EMsgNetworkConnect)
    {
        HandleConnect(steamId, messageRead);
        return;
    }

    if (!m_serverSteamId || steamId != m_serverSteamId)
    {
        Platform::Print("ClientGC: ignored message from {} (not our gs {})\n", steamId, m_serverSteamId);
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGC_IncrementKillCountAttribute:
            IncrementKillCountAttribute(messageRead);
            return;
        }
    }

    Platform::Print("ClientGC::HandleNetMessage: unhandled protobuf message {}\n",
        MessageName(messageRead.TypeUnmasked()));
}

void ClientGC::SetAuthTicket(uint32_t handle, std::vector<uint8_t> &&buffer)
{
    AuthTicket &ticket = m_tickets[handle];
    ticket.steamId = 0;
    ticket.buffer = std::move(buffer);
}

void ClientGC::ClearAuthTicket(uint32_t handle)
{
    auto it = m_tickets.find(handle);
    if (it == m_tickets.end())
    {
        assert(false);
        Platform::Print("ClientGC: tried to clear a nonexistent auth ticket???\n");
        return;
    }

    if (it->second.steamId)
    {
        Platform::Print("ClientGC: closing p2p session with {}\n", it->second.steamId);
        PostToHost(HostEvent::CloseChannel, it->second.steamId, {});

        if (it->second.steamId == m_serverSteamId)
        {
            Platform::Print("ClientGC: clearing gs identity\n");
            m_serverSteamId = 0;

            // kind of cringe to store this in inventory...
            m_inventory.ResetGameServerCache();
        }
    }

    m_tickets.erase(it);
}

void ClientGC::HandleConnect(uint64_t steamId, GCMessageRead &message)
{
    uint32_t ticketSize = message.ReadUint32();
    const void *ticket = message.ReadData(ticketSize);
    if (!message.IsValid())
    {
        Platform::Print("ClientGC: ignored connection from {} (malformed message)\n", steamId);
        return;
    }

    for (auto &[handle, authTicket] : m_tickets)
    {
        if (authTicket.buffer.size() == ticketSize && !memcmp(authTicket.buffer.data(), ticket, ticketSize))
        {
            authTicket.steamId = steamId;
            m_serverSteamId = steamId;
            Platform::Print("ClientGC: sending socache to {}\n", steamId);
            HandleSOCacheRequest();
            return;
        }
    }

    Platform::Print("ClientGC: ignored connection from {} (ticket mismatch)\n", steamId);
}

void ClientGC::HandleSOCacheRequest()
{
    CMsgSOCacheSubscribed message;
    m_inventory.BuildCacheSubscription(message, m_level, true);

    GCMessageWrite messageWrite{ k_ESOMsg_CacheSubscribed, message };
    PostToHost(HostEvent::NetMessage, m_serverSteamId, std::move(messageWrite).TakeBuffer());
}

void ClientGC::InventoryUpdate()
{
    InventoryChangeMessages changeMessages;
    if (m_inventory.Update(changeMessages))
    {
        SendInventoryChangeMessages(changeMessages);
    }
}

void ClientGC::SendMessageToGame(bool server, uint32_t type, const google::protobuf::MessageLite &message, uint64_t jobId)
{
    GCMessageWrite messageWrite{ type, message, jobId };

    if (server)
    {
        if (m_serverSteamId)
        {
            PostToHost(HostEvent::NetMessage, m_serverSteamId, std::move(messageWrite).TakeBuffer());
        }
    }
    else
    {
        uint32_t typeMasked = messageWrite.TypeMasked();
        PostToHost(HostEvent::Message, typeMasked, std::move(messageWrite).TakeBuffer());
    }
}

void ClientGC::SendSOCacheMessages(const SOCacheMessages &messages, bool server)
{
    for (const CMsgSOSingleObject &destroyed : messages.destroyed)
    {
        SendMessageToGame(server, k_ESOMsg_Destroy, destroyed);
    }

    for (const CMsgSOSingleObject &created : messages.created)
    {
        SendMessageToGame(server, k_ESOMsg_Create, created);
    }

    if (messages.updated.objects_modified_size())
    {
        SendMessageToGame(server, k_ESOMsg_UpdateMultiple, messages.updated);
    }
}

void ClientGC::SendInventoryChangeMessages(const InventoryChangeMessages &result)
{
    SendSOCacheMessages(result.client, false);
    SendSOCacheMessages(result.gameServer, true);

    if (result.notification.has_request())
    {
        // never sent to the gameserver
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, result.notification);
    }
}

constexpr uint32_t MakeAddress(uint32_t v1, uint32_t v2, uint32_t v3, uint32_t v4)
{
    return v4 | (v3 << 8) | (v2 << 16) | (v1 << 24);
}

static void BuildCSWelcome(CMsgCStrike15Welcome &message)
{
    // mikkotodo cleanup dox
    message.set_store_item_hash(136617352);
    message.set_timeplayedconsecutively(0);
    message.set_time_first_played(1329845773);
    message.set_last_time_played(1680260376);
    message.set_last_ip_address(MakeAddress(127, 0, 0, 1));
}

void ClientGC::BuildMatchmakingHello(CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &message)
{
    message.set_account_id(AccountId());

    // this is the state of csgo matchmaking in 2024
    message.mutable_global_stats()->set_players_online(0);
    message.mutable_global_stats()->set_servers_online(0);
    message.mutable_global_stats()->set_players_searching(0);
    message.mutable_global_stats()->set_servers_available(0);
    message.mutable_global_stats()->set_ongoing_matches(0);
    message.mutable_global_stats()->set_search_time_avg(0);

    // don't write search_statistics

    message.mutable_global_stats()->set_main_post_url("");

    // bullshit
    message.mutable_global_stats()->set_required_appid_version(13857);
    message.mutable_global_stats()->set_pricesheet_version(1680057676); // mikkotodo revisit
    message.mutable_global_stats()->set_twitch_streams_version(2);

    // NOTE: on 2013 this is actually game_monthly_uniques!!! probably not read there so keep the event id
    // 21 is Paris 2023, the last major. the coin journal and store read it
    message.mutable_global_stats()->set_active_tournament_eventid(21);

    message.mutable_global_stats()->set_active_survey_id(0);
    message.mutable_global_stats()->set_required_appid_version2(13862); // csgo s2

    message.set_vac_banned(GetConfig().m_vacBanned);

    // set competitive rank and wins here for old builds
    const RankInfo competitive = CompetitiveRank();
    message.mutable_ranking()->set_account_id(AccountId());
    message.mutable_ranking()->set_rank_id(competitive.rankId);
    message.mutable_ranking()->set_wins(competitive.wins);

    message.mutable_commendation()->set_cmd_friendly(GetConfig().m_commendedFriendly);
    message.mutable_commendation()->set_cmd_teaching(GetConfig().m_commendedTeaching);
    message.mutable_commendation()->set_cmd_leader(GetConfig().m_commendedLeader);
    message.set_player_level(m_level);
    message.set_player_cur_xp(m_xp);
}

void ClientGC::BuildClientWelcome(CMsgClientWelcome &message, const CMsgCStrike15Welcome &csWelcome,
    const CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &matchmakingHello)
{
    message.set_version(0); // this is accurate
    message.set_game_data(csWelcome.SerializeAsString());
    m_inventory.BuildCacheSubscription(*message.add_outofdate_subscribed_caches(), m_level, false);
    message.mutable_location()->set_latitude(65.0133006f);
    message.mutable_location()->set_longitude(25.4646212f);
    message.mutable_location()->set_country(GetConfig().m_country);
    message.set_game_data2(matchmakingHello.SerializeAsString());
    message.set_rtime32_gc_welcome_timestamp(static_cast<uint32_t>(time(nullptr)));
    message.set_currency(GetConfig().m_currency);
    message.set_txn_country_code(GetConfig().m_country);
}

ClientGC::RankInfo ClientGC::CompetitiveRank() const
{
    if (GetConfig().m_rankProgress)
    {
        const uint32_t now = static_cast<uint32_t>(time(nullptr));
        return { m_progress.SkillGroup(RankedMode::Competitive, now), static_cast<uint32_t>(m_progress.Skill(RankedMode::Competitive).wins) };
    }

    return { GetConfig().m_competitiveRank, static_cast<uint32_t>(GetConfig().m_competitiveWins) };
}

ClientGC::RankInfo ClientGC::WingmanRank() const
{
    if (GetConfig().m_rankProgress)
    {
        const uint32_t now = static_cast<uint32_t>(time(nullptr));
        return { m_progress.SkillGroup(RankedMode::Wingman, now), static_cast<uint32_t>(m_progress.Skill(RankedMode::Wingman).wins) };
    }

    return { GetConfig().m_wingmanRank, static_cast<uint32_t>(GetConfig().m_wingmanWins) };
}

ClientGC::RankInfo ClientGC::DangerZoneRank() const
{
    // bots don't play danger zone, so a new account stays unranked there
    if (GetConfig().m_rankProgress)
    {
        return { DangerZoneRankNone, 0 };
    }

    return { GetConfig().m_dangerZoneRank, static_cast<uint32_t>(GetConfig().m_dangerZoneWins) };
}

void ClientGC::SendRankUpdate(RankType changedType, float rankChange)
{
    CMsgGCCStrike15_v2_ClientGCRankUpdate message;

    auto add = [&](RankType type, RankInfo info)
    {
        PlayerRankingInfo *rank = message.add_rankings();
        rank->set_account_id(AccountId());
        rank->set_rank_id(info.rankId);
        rank->set_wins(info.wins);
        rank->set_rank_type_id(type);
        if (type == changedType)
        {
            rank->set_rank_change(rankChange);
        }
    };

    add(RankTypeCompetitive, CompetitiveRank());
    add(RankTypeWingman, WingmanRank());
    add(RankTypeDangerZone, DangerZoneRank());

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientGCRankUpdate, message);
}

void ClientGC::SetLevel(int level, int xp)
{
    m_level = level;
    m_xp = xp;

    if (GetConfig().m_rankProgress)
    {
        m_progress.SetLevel(level, xp);
        m_progress.Save();
    }

    SendPlayerLevel();
}

void ClientGC::OnMatchResult(const MatchResult &result)
{
    if (!GetConfig().m_rankProgress)
    {
        return;
    }

    // like real CS:GO, practice with bots earns nothing
    if (!m_officialMatch.pending || m_officialMatch.mode != result.mode)
    {
        Platform::Print("Practice match against bots ended, only Official Matchmaking counts\n");
        return;
    }

    m_officialMatch.pending = false;
    Platform::Print("Official Matchmaking match ended\n");

    // the bots were players of a skill group
    MatchResult rated = result;
    rated.botRating = BotProfiles::TierRating(m_officialMatch.botTier);

    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const PlayerProgress::MatchUpdate update = m_progress.ApplyMatch(rated, now);
    m_progress.Save();

    m_level = m_progress.Level();
    m_xp = m_progress.Xp();
    SendPlayerLevel();

    const RankType type = result.mode == RankedMode::Wingman ? RankTypeWingman : RankTypeCompetitive;
    SendRankUpdate(type, static_cast<float>(static_cast<int>(update.newRank) - static_cast<int>(update.oldRank)));

    // the end of match skill group and XP screens, for a match the player is still in
    if (!result.abandoned)
    {
        const std::string xpMessage = EndOfMatch::XpUpdateMessage(AccountId(), update);
        PostToHost(HostEvent::UserMessage, CS_UM_XpUpdate, { xpMessage.begin(), xpMessage.end() });

        const std::string rankMessage = EndOfMatch::RankUpdateMessage(AccountId(), result.mode, update);
        PostToHost(HostEvent::UserMessage, CS_UM_ServerRankUpdate, { rankMessage.begin(), rankMessage.end() });
    }

    const char *outcome = "lost";
    if (result.abandoned)
    {
        outcome = "abandoned";
    }
    else if (result.outcome == MatchOutcome::Win)
    {
        outcome = "won";
    }
    else if (result.outcome == MatchOutcome::Tie)
    {
        outcome = "tied";
    }

    const SkillRating &skill = m_progress.Skill(result.mode);
    Platform::Print("{} {} match {}-{} against {} bots ({} kills, {} deaths, {} MVPs)\n",
        result.mode == RankedMode::Wingman ? "Wingman" : "Competitive", outcome, result.roundsWon, result.roundsLost,
        BotProfiles::TierName(m_officialMatch.botTier), result.kills, result.deaths, result.mvps);
    Platform::Print("Rating {:.0f} -> {:.0f}, skill group {} -> {}, {}/{} wins for a skill group\n",
        update.oldRating, update.newRating, FromEnum(update.oldRank), FromEnum(update.newRank),
        std::min(skill.wins, Progress::WinsForSkillGroup), Progress::WinsForSkillGroup);
    Platform::Print("+{} XP ({} earned, {} weekly bonus), level {} -> {} ({}/{} XP)\n",
        update.gainedXp, update.earnedXp, update.gainedXp - update.earnedXp,
        update.oldLevel, update.newLevel, m_xp, Progress::XpPerLevel);
}

void ClientGC::OnMatchmakingPreferences(std::string_view preferences)
{
    const size_t newline = preferences.find('\n');
    m_matchmakingPreferences.mode = preferences.substr(0, newline);
    m_matchmakingPreferences.flags = 0;
    if (newline != std::string_view::npos)
    {
        FromString(preferences.substr(newline + 1), m_matchmakingPreferences.flags);
    }
}

void ClientGC::SendMatchmakingStatus(int matchmaking, std::string_view error)
{
    // matchmaking 0 ends the search, 1 to 3 is searching and 4 connects to the reserved server
    CMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate update;
    update.set_matchmaking(matchmaking);
    if (matchmaking)
    {
        update.add_waiting_account_id_sessions(AccountId());
    }

    if (!error.empty())
    {
        update.set_error(std::string{ error });
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate, update);
}

void ClientGC::MatchmakingStart(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_MatchmakingStart message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_MatchmakingStart failed, ignoring\n");
        return;
    }

    Platform::Print("Matchmaking start: game type {:#x}, {} player(s), mode \"{}\", flags {}\n",
        message.game_type(), message.account_ids_size(), m_matchmakingPreferences.mode, m_matchmakingPreferences.flags);

    const std::optional<RankedMode> mode = BotMatchmaking::ModeFromSetting(m_matchmakingPreferences.mode);
    if (!GetConfig().m_rankProgress || !mode)
    {
        // the client turns this into "#SFUI_QMM_ERROR_FailedToSetupSearchData"
        Platform::Print("Only competitive and wingman can be played against bots, with rank_progress\n");
        SendMatchmakingStatus(0, "Failed to setup search data");
        return;
    }

    m_reservedServer.Close();

    m_search.searching = true;
    m_search.mode = *mode;
    m_search.maps = BotMatchmaking::MapsFromGameType(message.game_type(), *mode);
    m_search.gameType = message.game_type();

    // competitive's match length: long, short, or either (a long or short match, whichever)
    const uint32_t length = m_matchmakingPreferences.flags & BotMatchmaking::FlagsAnyLength;
    m_search.shortMatch = *mode == RankedMode::Competitive
        && (length == BotMatchmaking::FlagsShortMatch || (length == BotMatchmaking::FlagsAnyLength && Random::Uint32(0, 1)));
    m_search.foundAt = std::chrono::steady_clock::now() + std::chrono::seconds{ Random::Uint32(5, 20) };

    SendMatchmakingStatus(1);
}

void ClientGC::MatchmakingStop(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_MatchmakingStop message;
    messageRead.ReadProtobuf(message);

    if (m_search.searching)
    {
        Platform::Print("Matchmaking stopped\n");
        m_search.searching = false;
    }

    // after accepting the popup stops matchmaking and starts the map, so a match found earlier
    // stays pending (it's counted when it ends)
    m_reservedServer.Close();

    SendMatchmakingStatus(0);
}

void ClientGC::MatchmakingTick()
{
    const auto now = std::chrono::steady_clock::now();

    m_reservedServer.Update();

    // declined, or never started: the next local server is just a server again
    if (m_officialMatch.pending && !m_officialMatch.started && now > m_officialMatch.expires)
    {
        Platform::Print("The match matchmaking found was never played\n");
        m_officialMatch.pending = false;
        m_reservedServer.Close();
        PostToHost(HostEvent::PrepareOfficialMatch, 0, {});
    }

    if (!m_search.searching || now < m_search.foundAt)
    {
        return;
    }

    m_search.searching = false;

    // bots playing like players of your own skill group, for a match as even as matchmaking's
    const RankId botTier = BotProfiles::TierFor(m_progress.Skill(m_search.mode).rating);
    const std::string map = Random::Select(m_search.maps);
    const uint64_t reservationId = (static_cast<uint64_t>(Random::Uint32()) << 32) | Random::Uint32(1);

    m_officialMatch.pending = true;
    m_officialMatch.started = false;
    m_officialMatch.mode = m_search.mode;
    m_officialMatch.map = map;
    m_officialMatch.botTier = botTier;
    m_officialMatch.expires = now + std::chrono::minutes{ 5 };

    // accepting starts the map here, the game makes it an official matchmaking server with this
    // reservation once the player is in. a random place in it is a random team
    const int players = BotMatchmaking::PlayerCount(m_search.mode);
    const std::string reservationText = BotMatchmaking::Reservation(reservationId, AccountId(), m_search.mode,
        static_cast<int>(Random::Uint32(0, static_cast<uint32_t>(players - 1))));
    const std::string prepare = reservationText + '\n' + std::to_string(FromEnum(botTier)) + '\n'
        + std::to_string(players) + '\n' + (m_search.shortMatch ? "1" : "0");
    PostToHost(HostEvent::PrepareOfficialMatch, 0, { prepare.begin(), prepare.end() });

    // the server matchmaking reserved. the client checks with it that everyone's reservation went
    // through before showing the accept popup
    uint32_t ip = MakeAddress(127, 0, 0, 1);
    uint16_t port = m_reservedServer.Open(reservationId, players);
    if (port)
    {
        ip = m_reservedServer.Ip();
    }
    else
    {
        port = 27015;
    }

    CMsgGCCStrike15_v2_MatchmakingGC2ClientReserve reserve;
    reserve.set_serverid(0x0184000000000001ull); // an anonymous game server
    reserve.set_direct_udp_ip(ip);
    reserve.set_direct_udp_port(port);
    reserve.set_reservationid(reservationId);
    reserve.set_map(map);
    reserve.set_server_address(std::format("{}.{}.{}.{}:{}", ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, port));

    CMsgGCCStrike15_v2_MatchmakingGC2ServerReserve *reservation = reserve.mutable_reservation();
    reservation->add_account_ids(AccountId());
    reservation->set_game_type(m_search.gameType);
    reservation->set_match_id(reserve.reservationid());

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientReserve, reserve);

    Platform::Print("Match found: {}{} on {} against {} bots\n",
        m_search.mode == RankedMode::Wingman ? "wingman" : "competitive", m_search.shortMatch ? " (short)" : "",
        map, BotProfiles::TierName(botTier));
}

void ClientGC::SendPlayerLevel()
{
    // the client replaces its stored hello with this one, which is where it reads its own level from
    CMsgGCCStrike15_v2_MatchmakingGC2ClientHello mmHello;
    BuildMatchmakingHello(mmHello);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello, mmHello);

    // everything else (scoreboard, the game server) reads it from the persona data
    InventoryChangeMessages messages = m_inventory.UpdatePlayerLevel(m_level);
    SendInventoryChangeMessages(messages);
}

void ClientGC::OnClientHello(GCMessageRead &messageRead)
{
    CMsgClientHello hello;
    if (!messageRead.ReadProtobuf(hello))
    {
        Platform::Print("Parsing CMsgClientHello failed, ignoring\n");
        return;
    }

    // we don't care about anything in this message, just reply
    CMsgCStrike15Welcome csWelcome;
    BuildCSWelcome(csWelcome);

    CMsgGCCStrike15_v2_MatchmakingGC2ClientHello mmHello;
    BuildMatchmakingHello(mmHello);

    CMsgClientWelcome clientWelcome;
    BuildClientWelcome(clientWelcome, csWelcome, mmHello);

    SendMessageToGame(false, k_EMsgGCClientWelcome, clientWelcome);

    // the real gc sends this a bit later when it has more info to put on it
    // however we have everything at our fingertips so send it right away
    // mikkotodo is this even needed? k_EMsgGCClientWelcome should have it all already
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello, mmHello);

    // send all ranks here as well, it's a bit back and forth with real gc
    SendRankUpdate();
}

void ClientGC::OnMatchmakingHello()
{
    // only old builds explicitly request for this
    CMsgGCCStrike15_v2_MatchmakingGC2ClientHello mmHello;
    BuildMatchmakingHello(mmHello);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello, mmHello);
}

void ClientGC::OnRequestInventoryRefresh()
{
    // only pre dota gcsdk merge builds send this
    m_inventory.SetOldProtocol(true);

    CMsgSOCacheSubscribed message;
    m_inventory.BuildCacheSubscription(message, m_level, false);
    SendMessageToGame(false, k_ESOMsg_CacheSubscribed, message);
}

void ClientGC::AdjustItemEquippedState(GCMessageRead &messageRead)
{
    CMsgAdjustItemEquippedState message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgAdjustItemEquippedState failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.EquipItem(message.item_id(), message.new_class(), message.new_slot());
    SendInventoryChangeMessages(messages);
}

void ClientGC::ClientPlayerDecalSign(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientPlayerDecalSign message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientPlayerDecalSign failed, ignoring\n");
        return;
    }

    if (!Graffiti::SignMessage(*message.mutable_data()))
    {
        Platform::Print("Could not sign graffiti! it won't appear\n");
        return;
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientPlayerDecalSign, message);
}

void ClientGC::UseItemRequest(GCMessageRead &messageRead)
{
    CMsgUseItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgUseItem failed, ignoring\n");
        return;
    }

    ItemDefIndex usedDefIndex;
    InventoryChangeMessages messages = m_inventory.UseItem(message.item_id(), usedDefIndex);
    SendInventoryChangeMessages(messages);

    if (usedDefIndex == ItemDefIndex::BonusRank)
    {
        SetLevel(std::min(m_level + 1, MaxPlayerLevel), 0);

        // shows the "new rank" popup, the client checks for a zero result and the bonus rank def index
        GCMessageWrite response{ k_EMsgGCUseItemResponse };
        response.WriteUint32(0);
        response.WriteUint32(FromEnum(ItemDefIndex::BonusRank));

        uint32_t typeMasked = response.TypeMasked();
        PostToHost(HostEvent::Message, typeMasked, std::move(response).TakeBuffer());

        Platform::Print("Used a bonus rank, level {}{}\n", m_level, GetConfig().m_rankProgress ? "" : " until restart");
    }
}

void ClientGC::StatTrakSwap(GCMessageRead &messageRead)
{
    CMsgApplyStatTrakSwap message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgApplyStatTrakSwap failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.StatTrakSwap(
        message.tool_item_id(),
        message.item_1_item_id(),
        message.item_2_item_id());

    SendInventoryChangeMessages(messages);
}

void ClientGC::ModifyItemAttribute(GCMessageRead &messageRead)
{
    CMsgModifyItemAttribute message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgModifyItemAttribute failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.ModifyItemAttribute(
        message.item_id(),
        message.attr_defidx(),
        message.attr_value());

    SendInventoryChangeMessages(messages);
}

// k_EMsgGCCStrike15_ClientDeepStats is deliberately left unanswered. The stats page keeps asking for
// the match list overview (no account id, no range) until the client has at least one match from it,
// and answering with nothing makes it ask again every frame. Without an answer it times out instead.
// Making the page work needs a made up match history.

void ClientGC::RecurringSubscriptionStatus()
{
    // the client asks whether its stats subscription renews. without an answer it keeps asking and
    // the stats page waits. a nonzero agreement means enrolled, active means it renews automatically
    // (matches the CSOAccountRecurringSubscription in the cache, see Inventory::BuildCacheSubscription)
    CMsgGCHRecurringSubscriptionStatusChange response;
    response.set_steamid(m_steamId);
    response.set_appid(730);
    response.set_agreementid(1);
    response.set_active(true);

    SendMessageToGame(false, k_EGCMsgRecurringSubscriptionStatusChange, response);
    Platform::Print("Answered stats subscription status: renews automatically\n");
}

void ClientGC::AdjustItemEquippedStateMulti(GCMessageRead &messageRead)
{
    // sent by loadout shuffle, at most once a minute outside of games
    CMsgAdjustItemEquippedStateMulti message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgAdjustItemEquippedStateMulti failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.EquipItemsMulti(message);
    SendInventoryChangeMessages(messages);
}

// commends left to give, the commend popup shows this. there's no weekly limit here
constexpr uint32_t CommendationTokens = 3;

void ClientGC::CommendPlayerQuery(GCMessageRead &messageRead)
{
    // the commend popup asks how many commends we have left and what we gave this player before,
    // it shows "Failed to load commendations" after 10 seconds without an answer
    CMsgGCCStrike15_v2_ClientCommendPlayer request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing commend query failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_ClientCommendPlayer response;
    response.set_account_id(request.account_id());
    response.set_tokens(CommendationTokens);

    auto it = m_commendations.find(request.account_id());
    if (it != m_commendations.end())
    {
        *response.mutable_commendation() = it->second;
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientCommendPlayerQueryResponse, response);
}

void ClientGC::CommendPlayer(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientCommendPlayer message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientCommendPlayer failed, ignoring\n");
        return;
    }

    // only remembered for this session, the other player's commend counts come from their own config
    m_commendations[message.account_id()] = message.commendation();

    Platform::Print("Commended account {}: friendly {}, teaching {}, leader {}\n",
        message.account_id(),
        message.commendation().cmd_friendly(),
        message.commendation().cmd_teaching(),
        message.commendation().cmd_leader());
}

static void BuildTournamentMatchInfo(CDataGCCStrike15_v2_MatchInfo &info, const TournamentInfo &tournament, const TournamentMatch &match)
{
    info.set_matchid(match.matchId);
    info.set_matchtime(match.time);

    // the client only lists matches that have tv_time set
    WatchableMatchInfo *watchable = info.mutable_watchablematchinfo();
    watchable->set_tv_time((match.score0 + match.score1) * 110);
    watchable->set_game_map(std::string{ match.map });
    watchable->set_match_id(match.matchId);

    // the client reads the map, score and teams from the last round stats entry
    CMsgGCCStrike15_v2_MatchmakingServerRoundStats *stats = info.add_roundstatsall();
    stats->set_map(std::string{ match.map });
    stats->add_team_scores(match.score0);
    stats->add_team_scores(match.score1);
    stats->set_match_result(match.score0 > match.score1 ? 1 : 2);

    CMsgGCCStrike15_v2_MatchmakingGC2ServerReserve *reservation = stats->mutable_reservation();
    reservation->set_match_id(match.matchId);

    TournamentEvent *event = reservation->mutable_tournament_event();
    event->set_event_id(tournament.eventId);
    event->set_event_name(std::string{ tournament.name });
    event->set_event_stage_id(match.stageId);

    for (const TournamentTeamInfo *team : { match.team0, match.team1 })
    {
        TournamentTeam *tournamentTeam = reservation->add_tournament_teams();
        tournamentTeam->set_team_id(team->id);
        tournamentTeam->set_team_tag(std::string{ team->tag });
        tournamentTeam->set_team_name(std::string{ team->name });
    }
}

void ClientGC::MatchListRequestTournamentGames(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_MatchListRequestTournamentGames request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_MatchListRequestTournamentGames failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_MatchList response;
    response.set_msgrequestid(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames);
    response.set_accountid(request.eventid()); // the client keys tournament lists by event id here
    response.set_servertime(static_cast<uint32_t>(time(nullptr)));

    // events we have no results for get an empty list
    const TournamentInfo *tournament = FindTournament(request.eventid());
    if (tournament)
    {
        for (const TournamentMatch &match : tournament->matches)
        {
            BuildTournamentMatchInfo(*response.add_matches(), *tournament, match);
        }
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchList, response);
}

void ClientGC::MatchListRequestTournamentPredictions(GCMessageRead &messageRead)
{
    // the tournament page asks for our Pick'em picks every few seconds until it has them. the GC
    // answers with the same message, and we never made any. the Pick'em tabs still can't show
    // without the event's Pick'em layout, which we don't have, but the asking stops
    CMsgGCCStrike15_v2_Predictions request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_Predictions failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_Predictions response;
    response.set_event_id(request.event_id());
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions, response);
}

void ClientGC::RequestSouvenir(GCMessageRead &messageRead)
{
    // redeeming a souvenir package from a tournament match with a coin
    CMsgGCCStrike15_v2_ClientRequestSouvenir request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientRequestSouvenir failed, ignoring\n");
        return;
    }

    const TournamentMatch *match = FindTournamentMatch(request.eventid(), request.matchid());
    if (!match)
    {
        Platform::Print("No match {} at event {}, ignoring souvenir request\n", request.matchid(), request.eventid());
        return;
    }

    uint64_t packageId;
    InventoryChangeMessages messages = m_inventory.RedeemSouvenir(request.itemid(), request.eventid(), *match, packageId);
    if (packageId)
    {
        SendInventoryChangeMessages(messages);
    }
}

static void AddressString(uint32_t ip, uint32_t port, char *buffer, size_t bufferSize)
{
    snprintf(buffer, bufferSize,
        "%u.%u.%u.%u:%u\n",
        (ip >> 24) & 0xff,
        (ip >> 16) & 0xff,
        (ip >> 8) & 0xff,
        ip & 0xff,
        port);
}

void ClientGC::ClientRequestJoinServerData(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientRequestJoinServerData request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientRequestJoinServerData failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_ClientRequestJoinServerData response = request;
    response.mutable_res()->set_serverid(request.version());
    response.mutable_res()->set_direct_udp_ip(request.server_ip());
    response.mutable_res()->set_direct_udp_port(request.server_port());
    response.mutable_res()->set_reservationid(GameServerCookieId);

    char addressString[32];
    AddressString(request.server_ip(), request.server_port(), addressString, sizeof(addressString));
    response.mutable_res()->set_server_address(addressString);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientRequestJoinServerData, response);
}

void ClientGC::SetItemPositions(GCMessageRead &messageRead)
{
    CMsgSetItemPositions message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgSetItemPositions failed, ignoring\n");
        return;
    }

    std::vector<CMsgItemAcknowledged> acknowledgements;
    acknowledgements.reserve(message.item_positions_size());

    InventoryChangeMessages messages = m_inventory.SetItemPositions(message, acknowledgements);

    // send these to the server only
    if (m_serverSteamId)
    {
        for (const CMsgItemAcknowledged &acknowledgement : acknowledgements)
        {
            {
                GCMessageWrite messageWrite{ k_EMsgGCItemAcknowledged, acknowledgement };
                PostToHost(HostEvent::NetMessage, m_serverSteamId, std::move(messageWrite).TakeBuffer());
            }

            // support for older builds...
            {
                CMsgItemAcknowledged__DEPRECATED deprecated;
                deprecated.set_account_id(acknowledgement.iteminfo().accountid());
                deprecated.set_inventory(acknowledgement.iteminfo().inventory());
                deprecated.set_def_index(acknowledgement.iteminfo().defindex());
                deprecated.set_quality(acknowledgement.iteminfo().quality());
                deprecated.set_rarity(acknowledgement.iteminfo().rarity());
                deprecated.set_origin(acknowledgement.iteminfo().origin());
                deprecated.set_item_id(acknowledgement.iteminfo().itemid());

                GCMessageWrite messageWrite{ k_EMsgGCItemAcknowledged__DEPRECATED, deprecated };
                PostToHost(HostEvent::NetMessage, m_serverSteamId, std::move(messageWrite).TakeBuffer());
            }
        }
    }

    SendInventoryChangeMessages(messages);
}

void ClientGC::IncrementKillCountAttribute(GCMessageRead &messageRead)
{
    CMsgIncrementKillCountAttribute message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgIncrementKillCountAttribute failed, ignoring\n");
        return;
    }

    assert(message.event_type() == 0);

    InventoryChangeMessages messages = m_inventory.IncrementKillCountAttribute(message.item_id(), message.amount());
    SendInventoryChangeMessages(messages);
}

void ClientGC::ApplySticker(GCMessageRead &messageRead)
{
    CMsgApplySticker message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgApplySticker failed, ignoring\n");
        return;
    }

    assert(!message.item_item_id() != !message.baseitem_defidx());

    InventoryChangeMessages messages;
    if (!message.sticker_item_id())
    {
        // scrape
        messages = m_inventory.ScrapeSticker(message);
    }
    else
    {
        messages = m_inventory.ApplySticker(message);
    }

    SendInventoryChangeMessages(messages);
}

void ClientGC::StoreGetUserData(GCMessageRead &messageRead)
{
    CMsgStoreGetUserData message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgStoreGetUserData failed, ignoring\n");
        return;
    }

    KeyValueRead priceSheetRead{ "csgo_gc/price_sheet.txt" };
    if (!priceSheetRead.Good())
    {
        return;
    }

    std::string binaryString;
    binaryString.reserve(1 << 17);
    priceSheetRead.Root().BinaryWriteToString(binaryString);

    // fuck you idiot
    CMsgStoreGetUserDataResponse response;
    response.set_result(1);
    response.set_currency_deprecated(GetConfig().m_currency); // for old builds
    response.set_country_deprecated(GetConfig().m_country); // for old builds
    response.set_price_sheet_version(1729); // what
    *response.mutable_price_sheet() = std::move(binaryString);

    SendMessageToGame(false, k_EMsgGCStoreGetUserDataResponse, response, messageRead.JobId());
}

void ClientGC::StorePurchaseInit(GCMessageRead &messageRead)
{
    CMsgGCStorePurchaseInit message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCStorePurchaseInit failed, ignoring\n");
        return;
    }

    // value doesn't matter
    assert(!m_transactionId);
    m_transactionId = Random::Uint32(1, UINT32_MAX);

    std::vector<uint32_t> purchasedDefIndexes;

    for (const auto &item : message.line_items())
    {
        for (uint32_t i = 0; i < item.quantity(); i++)
        {
            purchasedDefIndexes.push_back(item.item_def_id());
        }
    }

    InventoryChangeMessages messages = m_inventory.PurchaseItems(purchasedDefIndexes, m_transactionItemIds);

    char url[128]; // url doesn't matter, but it needs to be set
    snprintf(url, sizeof(url), "https://checkout.steampowered.com/checkout/approvetxn/%llu/?returnurl=steam", m_transactionId);

    CMsgGCStorePurchaseInitResponse response;
    response.set_result(1); // success
    response.set_txn_id(m_transactionId);
    response.set_url(url);
    response.mutable_item_ids()->Assign(m_transactionItemIds.begin(), m_transactionItemIds.end());

    SendMessageToGame(false, k_EMsgGCStorePurchaseInitResponse, response, messageRead.JobId());

    SendInventoryChangeMessages(messages);

    // this will run the steam callback
    PostToHost(HostEvent::MicroTransactionResponse, 0, {});
}

void ClientGC::StorePurchaseFinalize(GCMessageRead &messageRead)
{
    CMsgGCStorePurchaseFinalize message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCStorePurchaseFinalize failed, ignoring\n");
        return;
    }

    assert(m_transactionId);

    CMsgGCStorePurchaseFinalizeResponse response;
    response.set_result(1); // success
    response.mutable_item_ids()->Assign(m_transactionItemIds.begin(), m_transactionItemIds.end());
    SendMessageToGame(false, k_EMsgGCStorePurchaseFinalizeResponse, response, messageRead.JobId());

    // done with this one
    m_transactionId = 0;
}

void ClientGC::StorePurchaseFinalizeStruct(GCMessageRead &messageRead)
{
    messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCStorePurchaseFinalize (struct) failed, ignoring\n");
        return;
    }

    assert(m_transactionId);

    {
        GCMessageWrite response{ k_EMsgGCStorePurchaseFinalizeResponse, messageRead.JobId() };

        response.WriteUint16(1); // success
        response.WriteUint32(static_cast<uint32_t>(m_transactionItemIds.size()));

        for (uint64_t id : m_transactionItemIds)
        {
            response.WriteUint64(id);
        }

        uint32_t typeMasked = response.TypeMasked();
        PostToHost(HostEvent::Message, typeMasked, std::move(response).TakeBuffer());
    }

    // done with this one
    m_transactionId = 0;
}

void ClientGC::OnGetEventFavorites(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_GetEventFavorites_Request request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing GetEventFavorites failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_GetEventFavorites_Response response;
    response.set_all_events(request.all_events());
    response.set_json_favorites("[]");
    response.set_json_featured("[]");

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_GetEventFavorites_Response, response);
}

void ClientGC::OnPartyRegister(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_Party_Register message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Failed to parse Party_Register\n");
        return;
    }

    uint32_t lobbyId = message.id();
    if (!lobbyId)
    {
        lobbyId = Random::Uint32(1, UINT32_MAX);
    }

    CMsgGCCStrike15_v2_Party_Register response;
    response.set_id(lobbyId);
    response.set_ver(message.ver());
    response.set_apr(1);
    response.set_ark(CompetitiveRank().rankId * 10);
    response.set_grp(3);
    response.set_slots(5);
    response.set_launcher(message.launcher());
    response.set_game_type(message.game_type());

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_Party_Register, response);
}

// Service medals take two requests with the same message. The client first asks without a def index,
// and we answer with the medal it can get and the item it would upgrade. Its popup then asks again
// with that def index to claim it, and we grant or upgrade the medal, reset the level and answer again
void ClientGC::RequestPrestigeCoin(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin failed, ignoring\n");
        return;
    }

    Platform::Print("Service medal request: def index {}, level {}\n", request.defindex(), m_level);

    if (m_level < MaxPlayerLevel)
    {
        Platform::Print("Ignoring service medal request at level {}, need {}\n", m_level, MaxPlayerLevel);
        return;
    }

    ItemDefIndex offerDefIndex;
    uint64_t upgradeItemId;
    if (!m_inventory.GetPrestigeCoinOffer(offerDefIndex, upgradeItemId))
    {
        Platform::Print("No service medals in the item schema, ignoring request\n");
        return;
    }

    CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin response;

    if (!request.defindex())
    {
        response.set_defindex(FromEnum(offerDefIndex));
        response.set_upgradeid(upgradeItemId);
        SendMessageToGame(false, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, response);
        Platform::Print("Offered service medal {} (upgrading item {})\n", FromEnum(offerDefIndex), upgradeItemId);
        return;
    }

    uint64_t coinItemId;
    InventoryChangeMessages messages = m_inventory.ClaimPrestigeCoin(ToEnum<ItemDefIndex>(request.defindex()), coinItemId);
    if (!coinItemId)
    {
        Platform::Print("Ignoring claim for service medal {}, expected {}\n", request.defindex(), FromEnum(offerDefIndex));
        return;
    }

    // the item has to reach the client before the answer, which opens a preview of it
    SendInventoryChangeMessages(messages);

    SetLevel(1, 0);

    response.set_defindex(request.defindex());
    response.set_upgradeid(coinItemId);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, response);

    Platform::Print("Claimed service medal {}, level reset to 1{}\n", request.defindex(), GetConfig().m_rankProgress ? "" : " until restart");
}

void ClientGC::CraftItem(GCMessageRead &messageRead)
{
    /*uint16_t recipeDefIndex =*/messageRead.ReadUint16();
    uint16_t itemIdCount = messageRead.ReadUint16();
    const void *itemIdsPtr = messageRead.ReadData(itemIdCount * sizeof(uint64_t));
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCCraft failed, ignoring\n");
        return;
    }

    auto itemIds = std::make_unique_for_overwrite<uint64_t[]>(itemIdCount);
    memcpy(itemIds.get(), itemIdsPtr, itemIdCount * sizeof(uint64_t));

    uint64_t outputItemId = 0;
    InventoryChangeMessages messages = m_inventory.CraftItem({ itemIds.get(), itemIdCount }, outputItemId);
    if (!outputItemId)
    {
        // used to answer with an uninitialized item id here, the client times out with an error instead
        Platform::Print("Crafting failed, not answering\n");
        return;
    }

    SendInventoryChangeMessages(messages);

    {
        GCMessageWrite craftResponse{ k_EMsgGCCraftResponse };
        craftResponse.WriteUint32(0); // wtf
        craftResponse.WriteUint16(0); // wtf
        craftResponse.WriteUint16(1); // item count
        craftResponse.WriteUint64(outputItemId);

        uint32_t typeMasked = craftResponse.TypeMasked();
        PostToHost(HostEvent::Message, typeMasked, std::move(craftResponse).TakeBuffer());
    }
}

void ClientGC::DeleteItem(GCMessageRead &messageRead)
{
    // there is data after this, but i don't know what it is
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCDelete failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.RemoveItem(itemId);
    SendInventoryChangeMessages(messages);
}

void ClientGC::UnlockCrate(GCMessageRead &messageRead)
{
    uint64_t keyId = messageRead.ReadUint64();
    uint64_t crateId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCUnlockCrate failed, ignoring\n");
        return;
    }

    Platform::Print("CASE OPENING {} with {}\n", crateId, keyId);

    InventoryChangeMessages messages = m_inventory.UnlockCrate(crateId, keyId);
    SendInventoryChangeMessages(messages);

    // send this for old builds
    // FIXME: payload contents? don't think game reads it so we
    // can just pass an empty protobuf though... should probably
    // have a more idiomatic way of sending empty messages
    CMsgRequestInventoryRefresh response;
    SendMessageToGame(false, k_EMsgGCUnlockCrateResponse, response);
}

void ClientGC::NameItem(GCMessageRead &messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint64_t itemId = messageRead.ReadUint64();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameItem failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.NameItem(nameTagId, itemId, name);
    SendInventoryChangeMessages(messages);
}

void ClientGC::NameBaseItem(GCMessageRead &messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint32_t defIndex = messageRead.ReadUint32();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameBaseItem failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.NameBaseItem(nameTagId, defIndex, name);
    SendInventoryChangeMessages(messages);
}

void ClientGC::RemoveItemName(GCMessageRead &messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCRemoveItemName failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.RemoveItemName(itemId);
    SendInventoryChangeMessages(messages);
}

void ClientGC::ProcessCasketItemLoadContents(GCMessageRead &messageRead)
{
    CMsgCasketItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgCasketItem failed, ignoring\n");
        return;
    }

    CMsgGCItemCustomizationNotification notification;
    notification.set_request(k_EGCItemCustomizationNotification_CasketContents);
    notification.add_item_id(message.casket_item_id());
    SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
}

void ClientGC::ProcessCasketItemAdd(GCMessageRead &messageRead)
{
    CMsgCasketItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgCasketItem failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.CasketItemAdd(message.casket_item_id(), message.item_item_id());
    SendInventoryChangeMessages(messages);
}

void ClientGC::ProcessCasketItemExtract(GCMessageRead &messageRead)
{
    CMsgCasketItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgCasketItem failed, ignoring\n");
        return;
    }

    InventoryChangeMessages messages = m_inventory.CasketItemExtract(message.casket_item_id(), message.item_item_id());
    SendInventoryChangeMessages(messages);
}

// also used by gc_server.cpp

const char *MessageName(uint32_t type)
{
    switch (type)
    {
#define HANDLE_MSG(e) \
    case e: \
        return #e
        HANDLE_MSG(k_EMsgGCSystemMessage); // 4001;
        HANDLE_MSG(k_EMsgGCReplicateConVars); // 4002;
        HANDLE_MSG(k_EMsgGCConVarUpdated); // 4003;
        HANDLE_MSG(k_EMsgGCInQueue); // 4008;
        HANDLE_MSG(k_EMsgGCInviteToParty); // 4501;
        HANDLE_MSG(k_EMsgGCInvitationCreated); // 4502;
        HANDLE_MSG(k_EMsgGCPartyInviteResponse); // 4503;
        HANDLE_MSG(k_EMsgGCKickFromParty); // 4504;
        HANDLE_MSG(k_EMsgGCLeaveParty); // 4505;
        HANDLE_MSG(k_EMsgGCServerAvailable); // 4506;
        HANDLE_MSG(k_EMsgGCClientConnectToServer); // 4507;
        HANDLE_MSG(k_EMsgGCGameServerInfo); // 4508;
        HANDLE_MSG(k_EMsgGCError); // 4509;
        HANDLE_MSG(k_EMsgGCReplay_UploadedToYouTube); // 4510;
        HANDLE_MSG(k_EMsgGCLANServerAvailable); // 4511;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Base); // 9100;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingStart); // 9101;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingStop); // 9102;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingClient2ServerPing); // 9103;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate); // 9104;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingServerReservationResponse); // 9106;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientReserve); // 9107;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingClient2GCHello); // 9109;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello); // 9110;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientAbandon); // 9112;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingOperator2GCBlogUpdate); // 9117;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ServerNotificationForUserPenalty); // 9118;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientReportPlayer); // 9119;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientReportServer); // 9120;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientCommendPlayer); // 9121;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientReportResponse); // 9122;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientCommendPlayerQuery); // 9123;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientCommendPlayerQueryResponse); // 9124;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_WatchInfoUsers); // 9126;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestPlayersProfile); // 9127;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayersProfile); // 9128;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayerOverwatchCaseUpdate); // 9131;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayerOverwatchCaseAssignment); // 9132;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayerOverwatchCaseStatus); // 9133;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientTextMsg); // 9134;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCTextMsg); // 9135;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchEndRunRewardDrops); // 9136;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchEndRewardDropsNotification); // 9137;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestWatchInfoFriends2); // 9138;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchList); // 9139;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestCurrentLiveGames); // 9140;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestRecentUserGames); // 9141;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ServerReservationUpdate); // 9142;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientVarValueNotificationInfo); // 9144;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames); // 9146;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestFullGameInfo); // 9147;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GiftsLeaderboardRequest); // 9148;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GiftsLeaderboardResponse); // 9149;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ServerVarValueNotificationInfo); // 9150;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientSubmitSurveyVote); // 9152;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Server2GCClientValidate); // 9153;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestLiveGameForUser); // 9154;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCEconPreviewDataBlockRequest); // 9156;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCEconPreviewDataBlockResponse); // 9157;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_AccountPrivacySettings); // 9158;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_SetMyActivityInfo); // 9159;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions); // 9160;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListUploadTournamentPredictions); // 9161;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_DraftSummary); // 9162;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestJoinFriendData); // 9163;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestJoinServerData); // 9164;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestNewMission); // 9165;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientTournamentInfo); // 9167;
        HANDLE_MSG(k_EMsgGC_GlobalGame_Subscribe); // 9168;
        HANDLE_MSG(k_EMsgGC_GlobalGame_Unsubscribe); // 9169;
        HANDLE_MSG(k_EMsgGC_GlobalGame_Play); // 9170;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_AcknowledgePenalty); // 9171;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin); // 9172;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientGlobalStats); // 9173;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCStreamUnlock); // 9174;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_FantasyRequestClientData); // 9175;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_FantasyUpdateClientData); // 9176;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GCToClientSteamdatagramTicket); // 9177;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientToGCRequestTicket); // 9178;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientToGCRequestElevate); // 9179;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GlobalChat); // 9180;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GlobalChat_Subscribe); // 9181;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GlobalChat_Unsubscribe); // 9182;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientAuthKeyCode); // 9183;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GotvSyncPacket); // 9184;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientPlayerDecalSign); // 9185;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientLogonFatalError); // 9187;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientPollState); // 9188;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Register); // 9189;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Unregister); // 9190;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Search); // 9191;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Invite); // 9192;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Account_RequestCoPlays); // 9193;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientGCRankUpdate); // 9194;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestOffers); // 9195;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientAccountBalance); // 9196;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientPartyJoinRelay); // 9197;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientPartyWarning); // 9198;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_SetEventFavorite); // 9200;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GetEventFavorites_Request); // 9201;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientPerfReport); // 9202;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GetEventFavorites_Response); // 9203;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestSouvenir); // 9204;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientReportValidation); // 9205;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientRefuseSecureMode); // 9206;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientRequestValidation); // 9207;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRedeemMissionReward); // 9209;
        HANDLE_MSG(k_EMsgGCCStrike15_ClientDeepStats); // 9210;
        HANDLE_MSG(k_EMsgGCCStrike15_StartAgreementSessionInGame); // 9211;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientInitSystem); // 9212;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientInitSystem_Response); // 9213;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PrivateQueues); // 9214;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListTournamentOperatorMgmt); // 9215;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_BetaEnrollment); // 9217;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_SetPlayerLeaderboardSafeName); // 9218;
        HANDLE_MSG(k_EMsgGCBase); // 1000;
        HANDLE_MSG(k_EMsgGCSetItemPosition); // 1001;
        HANDLE_MSG(k_EMsgGCCraft); // 1002;
        HANDLE_MSG(k_EMsgGCCraftResponse); // 1003;
        HANDLE_MSG(k_EMsgGCDelete); // 1004;
        HANDLE_MSG(k_EMsgGCVerifyCacheSubscription); // 1005;
        HANDLE_MSG(k_EMsgGCNameItem); // 1006;
        HANDLE_MSG(k_EMsgGCUnlockCrate); // 1007;
        HANDLE_MSG(k_EMsgGCUnlockCrateResponse); // 1008;
        HANDLE_MSG(k_EMsgGCPaintItem); // 1009;
        HANDLE_MSG(k_EMsgGCPaintItemResponse); // 1010;
        HANDLE_MSG(k_EMsgGCGoldenWrenchBroadcast); // 1011;
        HANDLE_MSG(k_EMsgGCMOTDRequest); // 1012;
        HANDLE_MSG(k_EMsgGCMOTDRequestResponse); // 1013;
        HANDLE_MSG(k_EMsgGCAddItemToSocket_DEPRECATED); // 1014;
        HANDLE_MSG(k_EMsgGCAddItemToSocketResponse_DEPRECATED); // 1015;
        HANDLE_MSG(k_EMsgGCAddSocketToBaseItem_DEPRECATED); // 1016;
        HANDLE_MSG(k_EMsgGCAddSocketToItem_DEPRECATED); // 1017;
        HANDLE_MSG(k_EMsgGCAddSocketToItemResponse_DEPRECATED); // 1018;
        HANDLE_MSG(k_EMsgGCNameBaseItem); // 1019;
        HANDLE_MSG(k_EMsgGCNameBaseItemResponse); // 1020;
        HANDLE_MSG(k_EMsgGCRemoveSocketItem_DEPRECATED); // 1021;
        HANDLE_MSG(k_EMsgGCRemoveSocketItemResponse_DEPRECATED); // 1022;
        HANDLE_MSG(k_EMsgGCCustomizeItemTexture); // 1023;
        HANDLE_MSG(k_EMsgGCCustomizeItemTextureResponse); // 1024;
        HANDLE_MSG(k_EMsgGCUseItemRequest); // 1025;
        HANDLE_MSG(k_EMsgGCUseItemResponse); // 1026;
        HANDLE_MSG(k_EMsgGCGiftedItems_DEPRECATED); // 1027;
        HANDLE_MSG(k_EMsgGCRemoveItemName); // 1030;
        HANDLE_MSG(k_EMsgGCRemoveItemPaint); // 1031;
        HANDLE_MSG(k_EMsgGCGiftWrapItem); // 1032;
        HANDLE_MSG(k_EMsgGCGiftWrapItemResponse); // 1033;
        HANDLE_MSG(k_EMsgGCDeliverGift); // 1034;
        HANDLE_MSG(k_EMsgGCDeliverGiftResponseGiver); // 1035;
        HANDLE_MSG(k_EMsgGCDeliverGiftResponseReceiver); // 1036;
        HANDLE_MSG(k_EMsgGCUnwrapGiftRequest); // 1037;
        HANDLE_MSG(k_EMsgGCUnwrapGiftResponse); // 1038;
        HANDLE_MSG(k_EMsgGCSetItemStyle); // 1039;
        HANDLE_MSG(k_EMsgGCUsedClaimCodeItem); // 1040;
        HANDLE_MSG(k_EMsgGCSortItems); // 1041;
        HANDLE_MSG(k_EMsgGC_RevolvingLootList_DEPRECATED); // 1042;
        HANDLE_MSG(k_EMsgGCLookupAccount); // 1043;
        HANDLE_MSG(k_EMsgGCLookupAccountResponse); // 1044;
        HANDLE_MSG(k_EMsgGCLookupAccountName); // 1045;
        HANDLE_MSG(k_EMsgGCLookupAccountNameResponse); // 1046;
        HANDLE_MSG(k_EMsgGCUpdateItemSchema); // 1049;
        HANDLE_MSG(k_EMsgGCRemoveCustomTexture); // 1051;
        HANDLE_MSG(k_EMsgGCRemoveCustomTextureResponse); // 1052;
        HANDLE_MSG(k_EMsgGCRemoveMakersMark); // 1053;
        HANDLE_MSG(k_EMsgGCRemoveMakersMarkResponse); // 1054;
        HANDLE_MSG(k_EMsgGCRemoveUniqueCraftIndex); // 1055;
        HANDLE_MSG(k_EMsgGCRemoveUniqueCraftIndexResponse); // 1056;
        HANDLE_MSG(k_EMsgGCSaxxyBroadcast); // 1057;
        HANDLE_MSG(k_EMsgGCBackpackSortFinished); // 1058;
        HANDLE_MSG(k_EMsgGCAdjustItemEquippedState); // 1059;
        HANDLE_MSG(k_EMsgGCCollectItem); // 1061;
        HANDLE_MSG(k_EMsgGCItemAcknowledged__DEPRECATED); // 1062;
        HANDLE_MSG(k_EMsgGC_ReportAbuse); // 1065;
        HANDLE_MSG(k_EMsgGC_ReportAbuseResponse); // 1066;
        HANDLE_MSG(k_EMsgGCNameItemNotification); // 1068;
        HANDLE_MSG(k_EMsgGCApplyConsumableEffects); // 1069;
        HANDLE_MSG(k_EMsgGCConsumableExhausted); // 1070;
        HANDLE_MSG(k_EMsgGCShowItemsPickedUp); // 1071;
        HANDLE_MSG(k_EMsgGCClientDisplayNotification); // 1072;
        HANDLE_MSG(k_EMsgGCApplyStrangePart); // 1073;
        HANDLE_MSG(k_EMsgGC_IncrementKillCountAttribute); // 1074;
        HANDLE_MSG(k_EMsgGC_IncrementKillCountResponse); // 1075;
        HANDLE_MSG(k_EMsgGCApplyPennantUpgrade); // 1076;
        HANDLE_MSG(k_EMsgGCSetItemPositions); // 1077;
        HANDLE_MSG(k_EMsgGCApplyEggEssence); // 1078;
        HANDLE_MSG(k_EMsgGCNameEggEssenceResponse); // 1079;
        HANDLE_MSG(k_EMsgGCPaintKitItem); // 1080;
        HANDLE_MSG(k_EMsgGCPaintKitBaseItem); // 1081;
        HANDLE_MSG(k_EMsgGCPaintKitItemResponse); // 1082;
        HANDLE_MSG(k_EMsgGCGiftedItems); // 1083;
        HANDLE_MSG(k_EMsgGCUnlockItemStyle); // 1084;
        HANDLE_MSG(k_EMsgGCUnlockItemStyleResponse); // 1085;
        HANDLE_MSG(k_EMsgGCApplySticker); // 1086;
        HANDLE_MSG(k_EMsgGCItemAcknowledged); // 1087;
        HANDLE_MSG(k_EMsgGCStatTrakSwap); // 1088;
        HANDLE_MSG(k_EMsgGCUserTrackTimePlayedConsecutively); // 1089;
        HANDLE_MSG(k_EMsgGCItemCustomizationNotification); // 1090;
        HANDLE_MSG(k_EMsgGCModifyItemAttribute); // 1091;
        HANDLE_MSG(k_EMsgGCCasketItemAdd); // 1092;
        HANDLE_MSG(k_EMsgGCCasketItemExtract); // 1093;
        HANDLE_MSG(k_EMsgGCCasketItemLoadContents); // 1094;
        HANDLE_MSG(k_EMsgGCTradingBase); // 1500;
        HANDLE_MSG(k_EMsgGCTrading_InitiateTradeRequest); // 1501;
        HANDLE_MSG(k_EMsgGCTrading_InitiateTradeResponse); // 1502;
        HANDLE_MSG(k_EMsgGCTrading_StartSession); // 1503;
        HANDLE_MSG(k_EMsgGCTrading_SetItem); // 1504;
        HANDLE_MSG(k_EMsgGCTrading_RemoveItem); // 1505;
        HANDLE_MSG(k_EMsgGCTrading_UpdateTradeInfo); // 1506;
        HANDLE_MSG(k_EMsgGCTrading_SetReadiness); // 1507;
        HANDLE_MSG(k_EMsgGCTrading_ReadinessResponse); // 1508;
        HANDLE_MSG(k_EMsgGCTrading_SessionClosed); // 1509;
        HANDLE_MSG(k_EMsgGCTrading_CancelSession); // 1510;
        HANDLE_MSG(k_EMsgGCTrading_TradeChatMsg); // 1511;
        HANDLE_MSG(k_EMsgGCTrading_ConfirmOffer); // 1512;
        HANDLE_MSG(k_EMsgGCTrading_TradeTypingChatMsg); // 1513;
        HANDLE_MSG(k_EMsgGCServerBrowser_FavoriteServer); // 1601;
        HANDLE_MSG(k_EMsgGCServerBrowser_BlacklistServer); // 1602;
        HANDLE_MSG(k_EMsgGCServerRentalsBase); // 1700;
        HANDLE_MSG(k_EMsgGCItemPreviewCheckStatus); // 1701;
        HANDLE_MSG(k_EMsgGCItemPreviewStatusResponse); // 1702;
        HANDLE_MSG(k_EMsgGCItemPreviewRequest); // 1703;
        HANDLE_MSG(k_EMsgGCItemPreviewRequestResponse); // 1704;
        HANDLE_MSG(k_EMsgGCItemPreviewExpire); // 1705;
        HANDLE_MSG(k_EMsgGCItemPreviewExpireNotification); // 1706;
        HANDLE_MSG(k_EMsgGCItemPreviewItemBoughtNotification); // 1707;
        HANDLE_MSG(k_EMsgGCDev_NewItemRequest); // 2001;
        HANDLE_MSG(k_EMsgGCDev_NewItemRequestResponse); // 2002;
        HANDLE_MSG(k_EMsgGCDev_PaintKitDropItem); // 2003;
        HANDLE_MSG(k_EMsgGCStoreGetUserData); // 2500;
        HANDLE_MSG(k_EMsgGCStoreGetUserDataResponse); // 2501;
        HANDLE_MSG(k_EMsgGCStorePurchaseInit_DEPRECATED); // 2502;
        HANDLE_MSG(k_EMsgGCStorePurchaseInitResponse_DEPRECATED); // 2503;
        HANDLE_MSG(k_EMsgGCStorePurchaseFinalize); // 2504;
        HANDLE_MSG(k_EMsgGCStorePurchaseFinalizeResponse); // 2505;
        HANDLE_MSG(k_EMsgGCStorePurchaseCancel); // 2506;
        HANDLE_MSG(k_EMsgGCStorePurchaseCancelResponse); // 2507;
        HANDLE_MSG(k_EMsgGCStorePurchaseQueryTxn); // 2508;
        HANDLE_MSG(k_EMsgGCStorePurchaseQueryTxnResponse); // 2509;
        HANDLE_MSG(k_EMsgGCStorePurchaseInit); // 2510;
        HANDLE_MSG(k_EMsgGCStorePurchaseInitResponse); // 2511;
        HANDLE_MSG(k_EMsgGCBannedWordListRequest); // 2512;
        HANDLE_MSG(k_EMsgGCBannedWordListResponse); // 2513;
        HANDLE_MSG(k_EMsgGCToGCBannedWordListBroadcast); // 2514;
        HANDLE_MSG(k_EMsgGCToGCBannedWordListUpdated); // 2515;
        HANDLE_MSG(k_EMsgGCToGCDirtySDOCache); // 2516;
        HANDLE_MSG(k_EMsgGCToGCDirtyMultipleSDOCache); // 2517;
        HANDLE_MSG(k_EMsgGCToGCUpdateSQLKeyValue); // 2518;
        HANDLE_MSG(k_EMsgGCToGCIsTrustedServer); // 2519;
        HANDLE_MSG(k_EMsgGCToGCIsTrustedServerResponse); // 2520;
        HANDLE_MSG(k_EMsgGCToGCBroadcastConsoleCommand); // 2521;
        HANDLE_MSG(k_EMsgGCServerVersionUpdated); // 2522;
        HANDLE_MSG(k_EMsgGCToGCWebAPIAccountChanged); // 2524;
        HANDLE_MSG(k_EMsgGCRequestAnnouncements); // 2525;
        HANDLE_MSG(k_EMsgGCRequestAnnouncementsResponse); // 2526;
        HANDLE_MSG(k_EMsgGCRequestPassportItemGrant); // 2527;
        HANDLE_MSG(k_EMsgGCClientVersionUpdated); // 2528;
        HANDLE_MSG(k_EMsgGCAdjustItemEquippedStateMulti); // 2529;
        HANDLE_MSG(k_EMsgGCRecurringSubscriptionStatus); // 2530;
        HANDLE_MSG(k_EMsgGCAdjustEquipSlots); // 2531;
        HANDLE_MSG(k_EMsgGCClientWelcome); // 4004;
        HANDLE_MSG(k_EMsgGCServerWelcome); // 4005;
        HANDLE_MSG(k_EMsgGCClientHello); // 4006;
        HANDLE_MSG(k_EMsgGCServerHello); // 4007;
        HANDLE_MSG(k_EMsgGCClientConnectionStatus); // 4009;
        HANDLE_MSG(k_EMsgGCServerConnectionStatus); // 4010;
        HANDLE_MSG(k_EMsgGCClientHelloPartner); // 4011;
        HANDLE_MSG(k_EMsgGCClientHelloPW); // 4012;
        HANDLE_MSG(k_EMsgGCClientHelloR2); // 4013;
        HANDLE_MSG(k_EMsgGCClientHelloR3); // 4014;
        HANDLE_MSG(k_EMsgGCClientHelloR4); // 4015;
        HANDLE_MSG(k_EMsgUpdateSessionIP); // 154;
        HANDLE_MSG(k_EMsgRequestSessionIP); // 155;
        HANDLE_MSG(k_EMsgRequestSessionIPResponse); // 156;

        HANDLE_MSG(k_ESOMsg_Create);
        HANDLE_MSG(k_ESOMsg_Update);
        HANDLE_MSG(k_ESOMsg_Destroy);
        HANDLE_MSG(k_ESOMsg_CacheSubscribed);
        HANDLE_MSG(k_ESOMsg_CacheUnsubscribed);
        HANDLE_MSG(k_ESOMsg_UpdateMultiple);
        HANDLE_MSG(k_ESOMsg_CacheSubscriptionCheck);
        HANDLE_MSG(k_ESOMsg_CacheSubscriptionRefresh);
    }

    static thread_local char buffer[16];
    snprintf(buffer, sizeof(buffer), "%u", type);
    return buffer;
}
