#pragma once

// Official Matchmaking against bots (rank_progress): stands in for the official server the gc
// reserved for a match. Before the client shows the accept popup it checks with that server that
// everyone's reservation went through, and after accepting it checks again until everyone has
// accepted, which fills in the popup's players. Those checks are connectionless packets ('!'),
// the server answers how many players it's still waiting for ('%'). The match itself is played on
// the player's own server, which the accept popup starts (ui_patches)
namespace ReservationCheck
{

constexpr uint8_t RequestType = '!';
constexpr uint8_t ReplyType = '%';

// the client's two checks: everyone found the match, then everyone accepted it
constexpr uint32_t StageReserved = 1;
constexpr uint32_t StageAccepted = 2;

struct Request
{
    uint32_t hostVersion; // the reply has to match the client's
    uint32_t token; // echoed back
    uint32_t stage;
    uint64_t reservationId;
    uint64_t steamId;
};

std::optional<Request> ParseRequest(const uint8_t *data, size_t size);

// awaiting is how many players haven't checked in (or accepted) yet, 0 when everyone has
std::vector<uint8_t> Reply(const Request &request, uint8_t awaiting, uint8_t players);

} // namespace ReservationCheck

class ReservedServer : public NonCopyable
{
public:
    ReservedServer() = default;
    ~ReservedServer();

    // starts answering checks for this reservation on a free local port, returns the port or 0
    // if there's no socket
    uint16_t Open(uint64_t reservationId, int players);
    void Close();

    bool IsOpen() const { return m_socket != InvalidSocket; }

    // the address it listens on (host byte order). not 127.0.0.1 if possible: the engine keeps
    // packets to that address to itself (net_usesocketsforloopback 0), they never reach a socket
    uint32_t Ip() const { return m_ip; }

    // answers the checks that arrived, called every frame
    void Update();

private:
    static constexpr intptr_t InvalidSocket = -1;

    uint8_t Awaiting(uint32_t stage);

    intptr_t m_socket{ InvalidSocket };
    uint32_t m_ip{};
    uint64_t m_reservationId{};
    int m_players{};

    // the bots accept a moment after the player does
    std::optional<std::chrono::steady_clock::time_point> m_acceptedAt;

    uint32_t m_loggedStage{};
    uint8_t m_loggedAwaiting{};
};
