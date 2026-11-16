#pragma once

#include "item.h"

#include <span>

class ItemSchema;

// Major results we can show in Watch -> Tournament, which is where the client
// redeems souvenir packages from. Each map played is a match of its own.

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

// nullptr if we have no match data for the event
const TournamentInfo *FindTournament(uint32_t eventId);
const TournamentMatch *FindTournamentMatch(uint32_t eventId, uint64_t matchId);

// turns the item rolled from a souvenir package into a souvenir: tournament quality,
// no StatTrak, the package's event, stage and teams, and the event's gold stickers
void ApplySouvenirAttributes(const ItemSchema &itemSchema, const Item &package, ItemDesc &desc);
