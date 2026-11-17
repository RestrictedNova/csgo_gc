#include "stdafx.h"
#include "reserved_server.h"
#include "platform.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using NativeSocket = SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
#endif

namespace ReservationCheck
{

// connectionless packets start with -1
constexpr uint8_t Header[] = { 0xff, 0xff, 0xff, 0xff };

static uint32_t ReadUint32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
        | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

static void WriteUint32(std::vector<uint8_t> &out, uint32_t value)
{
    for (int i = 0; i < 4; i++)
    {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

std::optional<Request> ParseRequest(const uint8_t *data, size_t size)
{
    // header, type, host version, token, stage, reservation id, steam id (engine.dll's
    // CServerMsg_CheckReservation)
    constexpr size_t Size = sizeof(Header) + 1 + 4 * 3 + 8 * 2;
    if (size < Size || memcmp(data, Header, sizeof(Header)) || data[4] != RequestType)
    {
        return std::nullopt;
    }

    const uint8_t *fields = data + sizeof(Header) + 1;

    Request request;
    request.hostVersion = ReadUint32(fields);
    request.token = ReadUint32(fields + 4);
    request.stage = ReadUint32(fields + 8);
    request.reservationId = ReadUint32(fields + 12) | (static_cast<uint64_t>(ReadUint32(fields + 16)) << 32);
    request.steamId = ReadUint32(fields + 20) | (static_cast<uint64_t>(ReadUint32(fields + 24)) << 32);
    return request;
}

std::vector<uint8_t> Reply(const Request &request, uint8_t awaiting, uint8_t players)
{
    // what an official server answers: the client's version and token, the stage, then how many
    // of how many players it's waiting for
    std::vector<uint8_t> reply{ std::begin(Header), std::end(Header) };
    reply.push_back(ReplyType);
    WriteUint32(reply, request.hostVersion);
    WriteUint32(reply, request.token);
    WriteUint32(reply, request.stage);
    reply.push_back(awaiting);
    reply.push_back(players);
    return reply;
}

} // namespace ReservationCheck

static NativeSocket Native(intptr_t socket)
{
    return static_cast<NativeSocket>(socket);
}

static void CloseSocket(intptr_t socket)
{
#ifdef _WIN32
    closesocket(Native(socket));
#else
    close(Native(socket));
#endif
}

ReservedServer::~ReservedServer()
{
    Close();
}

uint16_t ReservedServer::Open(uint64_t reservationId, int players)
{
    Close();

#ifdef _WIN32
    // the game started winsock long ago, this just takes a reference
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        return 0;
    }

    const SOCKET handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET)
    {
        WSACleanup();
        return 0;
    }

    u_long nonBlocking = 1;
    ioctlsocket(handle, FIONBIO, &nonBlocking);
#else
    const int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle < 0)
    {
        return 0;
    }

    fcntl(handle, F_SETFL, fcntl(handle, F_GETFL, 0) | O_NONBLOCK);
#endif

    m_socket = static_cast<intptr_t>(handle);

    // a free port on loopback. the engine sends packets for 127.0.0.1 to itself instead of the
    // network, so 127.0.0.2 where there's one (windows and linux loop back all of 127.0.0.0/8)
    constexpr uint32_t Loopback2 = 0x7f000002;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(Loopback2);
    address.sin_port = 0;

    if (bind(handle, reinterpret_cast<const sockaddr *>(&address), sizeof(address)))
    {
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(handle, reinterpret_cast<const sockaddr *>(&address), sizeof(address)))
        {
            Platform::Print("Couldn't open a port for the matchmaking server\n");
            Close();
            return 0;
        }
    }

    socklen_t addressSize = sizeof(address);
    if (getsockname(handle, reinterpret_cast<sockaddr *>(&address), &addressSize))
    {
        Close();
        return 0;
    }

    m_ip = ntohl(address.sin_addr.s_addr);
    m_reservationId = reservationId;
    m_players = players;
    m_acceptedAt.reset();
    m_loggedStage = 0;
    m_loggedAwaiting = 0;

    return ntohs(address.sin_port);
}

void ReservedServer::Close()
{
    if (m_socket == InvalidSocket)
    {
        return;
    }

    CloseSocket(m_socket);
    m_socket = InvalidSocket;

#ifdef _WIN32
    WSACleanup();
#endif
}

uint8_t ReservedServer::Awaiting(uint32_t stage)
{
    if (stage != ReservationCheck::StageAccepted)
    {
        // everyone found the match
        return 0;
    }

    // the player accepted, the bots follow one by one
    const auto now = std::chrono::steady_clock::now();
    if (!m_acceptedAt)
    {
        m_acceptedAt = now;
    }

    constexpr auto BotAcceptInterval = std::chrono::milliseconds{ 120 };
    const int accepted = 1 + static_cast<int>((now - *m_acceptedAt) / BotAcceptInterval);

    // never everyone: that makes the client connect to this address, which has no game server.
    // the accept popup starts the match on the player's own server instead (ui_patches)
    return static_cast<uint8_t>(std::max(m_players - accepted, 1));
}

void ReservedServer::Update()
{
    if (m_socket == InvalidSocket)
    {
        return;
    }

    for (;;)
    {
        uint8_t packet[256];
        sockaddr_in from{};
        socklen_t fromSize = sizeof(from);
        const auto received = recvfrom(Native(m_socket), reinterpret_cast<char *>(packet), sizeof(packet), 0,
            reinterpret_cast<sockaddr *>(&from), &fromSize);
        if (received <= 0)
        {
            break;
        }

        const std::optional<ReservationCheck::Request> request
            = ReservationCheck::ParseRequest(packet, static_cast<size_t>(received));
        if (!request || request->reservationId != m_reservationId)
        {
            std::string bytes;
            for (int i = 0; i < std::min(static_cast<int>(received), 40); i++)
            {
                bytes += std::format("{:02x} ", packet[i]);
            }

            Platform::Print("Matchmaking server: ignored a {} byte packet: {}\n", received, bytes);
            continue;
        }

        const uint8_t awaiting = Awaiting(request->stage);
        const std::vector<uint8_t> reply = ReservationCheck::Reply(*request, awaiting, static_cast<uint8_t>(m_players));
        sendto(Native(m_socket), reinterpret_cast<const char *>(reply.data()), static_cast<int>(reply.size()), 0,
            reinterpret_cast<const sockaddr *>(&from), fromSize);

        // the client checks again every second or so, only log what changed
        if (request->stage != m_loggedStage || awaiting != m_loggedAwaiting)
        {
            m_loggedStage = request->stage;
            m_loggedAwaiting = awaiting;
            Platform::Print("Matchmaking server: reservation check {}, waiting for {} of {} players\n",
                request->stage, awaiting, m_players);
        }
    }
}
