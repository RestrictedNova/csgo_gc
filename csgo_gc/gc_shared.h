#pragma once

enum class HostEvent
{
    Message, // id contains the message type, buffer contains the payload
    NetMessage, // id contains the recipient steam id, buffer contains the payload
    MicroTransactionResponse, // runs MicroTxnAuthorizationResponse_t, no arguments
    CloseChannel, // id contains the steam id
    PrepareOfficialMatch, // buffer contains the reservation, the bots' skill group (RankId), player count and short match (0/1), one per line. empty cancels
    UserMessage, // id contains the user message type, buffer contains the message, for the client like a server would send it
};

enum class GCEvent
{
    // shared
    Message, // id contains the message type, buffer contains the payload
    NetMessage, // id contains the sender steam id, buffer contains the payload
    Tick, // sent when running callbacks

    // client
    SetAuthTicket, // id contains the ticket handle, buffer contains the ticket
    ClearAuthTicket, // id contains the ticket handle
    MatchResult, // buffer contains a MatchResult, a match against bots ended
    MatchmakingPreferences, // buffer contains the play menu's official mode and its flags (match length), one per line
    OfficialMatchStarted, // the match matchmaking found is being played, no arguments

    // server
    ClientConnected, // id contains the steam id, buffer contains the ticket
    ClientDisconnected, // id contains the steam id
    SessionRequest, // id contains the steam id
};

struct EventData
{
    int type; // HostEvent or GCEvent
    uint64_t id;
    std::vector<uint8_t> buffer;
};

// shared logic between client and server gcs
class SharedGC
{
public:
    void PostToGC(GCEvent type, uint64_t id, const void *data, uint32_t dataSize);

    void GetHostEvents(std::vector<EventData> &events);

protected:
    // must be manually called by the gc
    void StartThread();
    void StopThread();

    void PostToHost(HostEvent type, uint64_t id, std::vector<uint8_t> &&buffer);

private:
    virtual void HandleEvent(GCEvent type, uint64_t id, std::vector<uint8_t> &&buffer) = 0;

    void WorkerThread();

    std::mutex m_gcEventMutex;
    std::condition_variable m_cv;
    std::vector<EventData> m_gcEvents;
    std::thread m_thread;
    bool m_stopping{ false };

    std::mutex m_hostEventMutex;
    std::vector<EventData> m_hostEvents;
};

const char *MessageName(uint32_t type);
