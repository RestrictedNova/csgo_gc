#pragma once

#include "item.h"

#include <span>

class ItemSchema;

// Major results we show in Watch -> Tournaments, which is where the client redeems souvenir
// packages from, and that souvenirs get their match from. Each map played is a match of its own.

struct TournamentTeamInfo
{
    uint32_t id; // pro team id from the item schema
    std::string_view tag;
    std::string_view name;
};

struct TournamentMatch
{
    uint64_t matchId;
    uint32_t time;
    uint32_t stageId; // CSGO_Tournament_Event_Stage_<id>, one per map of a series
    const TournamentTeamInfo *team0;
    const TournamentTeamInfo *team1;
    std::string_view map;
    int score0;
    int score1;
};

struct TournamentInfo
{
    uint32_t eventId;
    std::string_view name;
    std::span<const TournamentMatch> matches;
};

// every major's results, generated into tournament_data.cpp by tools/tournaments
std::span<const TournamentInfo> AllTournaments();

// nullptr if we have no match data for the event
const TournamentInfo *FindTournament(uint32_t eventId);
const TournamentMatch *FindTournamentMatch(uint32_t eventId, uint64_t matchId);

// a random match of the event played on a souvenir package's map, nullptr if there are none
const TournamentMatch *RandomMatchForPackage(const ItemSchema &itemSchema, ItemDefIndex packageDefIndex, uint32_t eventId);

// turns the item rolled from a souvenir package into a souvenir like the real ones: tournament
// quality, no StatTrak, the package's event, and since DreamHack 2014 its stage and teams (a
// match on its map when the package wasn't redeemed from one) with that event's gold stickers:
// both teams and the organiser, plus the round MVP's autograph since Cologne 2015
void ApplySouvenirAttributes(const ItemSchema &itemSchema, const Item &package, ItemDesc &desc);
