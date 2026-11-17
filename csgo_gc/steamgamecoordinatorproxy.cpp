#include "stdafx.h"
#include "steamgamecoordinatorproxy.h"
#include "gc_client.h"
#include "gc_server.h"
#include "keyvalue.h"
#include "match_tracker.h"
#include "steam_callbacks.h"

struct SteamNetworkingIdentity;

#include <steam/steam_api.h>
#include <steam/steam_gameserver.h>

#include "networking_client.h"
#include "networking_server.h"

static uint64_t GetUserSteamId(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user)
{
    ISteamUser *interface = steamClient.GetISteamUser(user, pipe, STEAMUSER_INTERFACE_VERSION);
    if (!interface)
    {
        Platform::Error("Could not get %s", STEAMUSER_INTERFACE_VERSION);
    }

    CSteamID steamId = interface->GetSteamID();
    assert(steamId.IsValid());
    return steamId.ConvertToUint64();
}

static ISteamNetworkingMessages *GetSteamNetworkingMessages(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user)
{
    void *interface = steamClient.GetISteamGenericInterface(user, pipe, STEAMNETWORKINGMESSAGES_INTERFACE_VERSION);
    if (!interface)
    {
        Platform::Error("Could not get %s", STEAMNETWORKINGMESSAGES_INTERFACE_VERSION);
    }

    return static_cast<ISteamNetworkingMessages *>(interface);
}

static ISteamGameServer *GetSteamGameServer(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user)
{
    ISteamGameServer *interface = steamClient.GetISteamGameServer(user, pipe, STEAMGAMESERVER_INTERFACE_VERSION);
    if (!interface)
    {
        Platform::Error("Could not get %s", STEAMGAMESERVER_INTERFACE_VERSION);
    }

    return interface;
}

class SteamGameCoordinatorProxyClient final : public SteamGameCoordinatorProxy
{
public:
    SteamGameCoordinatorProxyClient(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user, CCallbackMgr &callbackMgr);
    ~SteamGameCoordinatorProxyClient();

    SharedGC &GetGC() override;
    void RunFrame() override;

private:
    void BeforeSendMessage(uint32_t type) override;

    ClientGC m_gc;
    NetworkingClient m_networking;
};

class SteamGameCoordinatorProxyServer final : public SteamGameCoordinatorProxy
{
public:
    SteamGameCoordinatorProxyServer(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user, CCallbackMgr &callbackMgr);

    SharedGC &GetGC() override;
    void RunFrame() override;

private:
    ServerGC m_gc;
    NetworkingServer m_networking;
    ISteamGameServer *const m_steamGameServer;
};

SteamGameCoordinatorProxy::SteamGameCoordinatorProxy(CCallbackMgr &callbackMgr)
    : m_callbackMgr{ callbackMgr }
{
}

SteamGameCoordinatorProxy::~SteamGameCoordinatorProxy()
{
}

PROXY_FUNC(EGCResults, SteamGameCoordinatorProxy::SendMessage, uint32 unMsgType, const void *pubData, uint32 cubData)
{
    BeforeSendMessage(unMsgType);
    GetGC().PostToGC(GCEvent::Message, unMsgType, pubData, cubData);
    return k_EGCResultOK;
}

PROXY_FUNC(bool, SteamGameCoordinatorProxy::IsMessageAvailable, uint32 *pcubMsgSize)
{
    if (m_messages.empty())
    {
        return false;
    }

    Message &message = m_messages.front();
    *pcubMsgSize = static_cast<uint32_t>(message.buffer.size());
    return true;
}

PROXY_FUNC(EGCResults, SteamGameCoordinatorProxy::RetrieveMessage, uint32 *punMsgType, void *pubDest, uint32 cubDest, uint32 *pcubMsgSize)
{
    if (m_messages.empty())
    {
        *pcubMsgSize = 0;
        return k_EGCResultNoMessage;
    }

    Message &message = m_messages.front();
    *punMsgType = message.type;
    *pcubMsgSize = static_cast<uint32_t>(message.buffer.size());

    if (cubDest < message.buffer.size())
    {
        return k_EGCResultBufferTooSmall;
    }

    memcpy(pubDest, message.buffer.data(), message.buffer.size());
    m_messages.pop_front();
    return k_EGCResultOK;
}

void SteamGameCoordinatorProxy::AddMessage(uint32_t type, std::vector<uint8_t> &&buffer)
{
    Message &dest = m_messages.emplace_back();
    dest.type = type;
    dest.buffer = std::move(buffer);

    GCMessageAvailable_t param{};
    param.m_nMessageSize = static_cast<uint32_t>(m_messages.front().buffer.size());
    m_callbackMgr.QueueCallback(param);
}

SteamGameCoordinatorProxyClient::SteamGameCoordinatorProxyClient(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user, CCallbackMgr &callbackMgr)
    : SteamGameCoordinatorProxy{ callbackMgr }
    , m_gc{ GetUserSteamId(steamClient, pipe, user) }
    , m_networking{ GetSteamNetworkingMessages(steamClient, pipe, user), m_gc }
{
}

SteamGameCoordinatorProxyClient::~SteamGameCoordinatorProxyClient()
{
    MatchWatcher::Shutdown();
}

SharedGC &SteamGameCoordinatorProxyClient::GetGC()
{
    return m_gc;
}

void SteamGameCoordinatorProxyClient::BeforeSendMessage(uint32_t type)
{
    // the gc can't see the play menu, so hand it the official mode and match length picked there
    // before the search starts (the ticked maps are in MatchmakingStart's game type). the final
    // build keeps the mode in a "_v20" convar
    if ((type & ~ProtobufMask) == k_EMsgGCCStrike15_v2_MatchmakingStart)
    {
        std::string mode = MatchWatcher::GetGameSetting("ui_playsettings_mode_official_v20");
        if (mode.empty())
        {
            mode = MatchWatcher::GetGameSetting("ui_playsettings_mode_official");
        }

        const std::string flags = MatchWatcher::GetGameSetting(("ui_playsettings_flags_official_" + mode).c_str());
        const std::string preferences = mode + '\n' + flags;
        m_gc.PostToGC(GCEvent::MatchmakingPreferences, 0, preferences.data(), static_cast<uint32_t>(preferences.size()));
    }
}

void SteamGameCoordinatorProxyClient::RunFrame()
{
    // matches against bots on a local server, for rank_progress
    MatchWatcher::Update(m_gc);

    // poll events
    std::vector<EventData> events;
    m_gc.GetHostEvents(events);

    for (EventData &event : events)
    {
        switch (static_cast<HostEvent>(event.type))
        {
        case HostEvent::Message:
            AddMessage(static_cast<uint32_t>(event.id), std::move(event.buffer));
            break;

        case HostEvent::NetMessage:
            m_networking.SendMessage(event.id, event.buffer.data(), static_cast<uint32_t>(event.buffer.size()));
            break;

        case HostEvent::CloseChannel:
            m_networking.CloseChannel(event.id);
            break;

        case HostEvent::MicroTransactionResponse:
        {
            Platform::Print("Spoofing MicroTxnAuthorizationResponse_t\n");

            MicroTxnAuthorizationResponse_t response{};
            response.m_bAuthorized = 1;
            m_callbackMgr.QueueCallback(response);
            break;
        }

        case HostEvent::PrepareOfficialMatch:
        {
            if (event.buffer.empty())
            {
                MatchWatcher::CancelOfficialMatch();
                break;
            }

            std::string_view lines{ reinterpret_cast<const char *>(event.buffer.data()), event.buffer.size() };
            std::vector<std::string_view> fields;
            while (!lines.empty())
            {
                const size_t end = lines.find('\n');
                fields.push_back(lines.substr(0, end));
                lines = end == std::string_view::npos ? std::string_view{} : lines.substr(end + 1);
            }

            if (fields.size() == 4)
            {
                MatchWatcher::OfficialMatchSettings settings;
                settings.reservation = fields[0];
                FromString(fields[1], settings.botDifficulty);
                FromString(fields[2], settings.players);
                settings.shortMatch = fields[3] == "1";
                MatchWatcher::PrepareOfficialMatch(settings);
            }
            break;
        }

        default:
            assert(false);
            break;
        }
    }

    // stupid hack to test the inventory editor
    m_gc.PostToGC(GCEvent::Tick, 0, nullptr, 0);

    // poll networking
    m_networking.Update();
}

SteamGameCoordinatorProxyServer::SteamGameCoordinatorProxyServer(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user, CCallbackMgr &callbackMgr)
    : SteamGameCoordinatorProxy{ callbackMgr }
    , m_networking{ GetSteamNetworkingMessages(steamClient, pipe, user), m_gc }
    , m_steamGameServer{ GetSteamGameServer(steamClient, pipe, user) }
{
}

SharedGC &SteamGameCoordinatorProxyServer::GetGC()
{
    return m_gc;
}

void SteamGameCoordinatorProxyServer::RunFrame()
{
    // only run server gc when logged on as an attempt to more accurately mimic real gc behaviour
    // FIXME: does csgo handle CMsgConnectionStatus?
    if (!m_steamGameServer->BLoggedOn())
    {
        return;
    }

    // poll events
    std::vector<EventData> events;
    m_gc.GetHostEvents(events);

    for (EventData &event : events)
    {
        switch (static_cast<HostEvent>(event.type))
        {
        case HostEvent::Message:
            AddMessage(static_cast<uint32_t>(event.id), std::move(event.buffer));
            break;

        case HostEvent::NetMessage:
            m_networking.SendMessage(event.id, event.buffer.data(), static_cast<uint32_t>(event.buffer.size()));
            break;

        case HostEvent::CloseChannel:
            m_networking.CloseChannel(event.id);
            break;

        default:
            assert(false);
            break;
        }
    }

    // FIXME: remove this later
    m_gc.PostToGC(GCEvent::Tick, 0, nullptr, 0);

    m_networking.Update();
}

std::unique_ptr<SteamGameCoordinatorProxy> CreateSteamGameCoordinatorProxyClient(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user, CCallbackMgr &callbackMgr)
{
    return std::make_unique<SteamGameCoordinatorProxyClient>(steamClient, pipe, user, callbackMgr);
}

std::unique_ptr<SteamGameCoordinatorProxy> CreateSteamGameCoordinatorProxyServer(ISteamClient &steamClient, HSteamPipe pipe, HSteamUser user, CCallbackMgr &callbackMgr)
{
    return std::make_unique<SteamGameCoordinatorProxyServer>(steamClient, pipe, user, callbackMgr);
}
