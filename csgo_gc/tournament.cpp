#include "stdafx.h"
#include "tournament.h"
#include "item_schema.h"

// BLAST.tv Paris Major 2023 (event 21), the last CS:GO major. Playoff results from the
// event's Wikipedia bracket, team ids and tags from items_game.txt. Days are right,
// start times are approximate.
namespace Paris2023
{

constexpr uint32_t EventId = 21;

constexpr TournamentTeamInfo Vitality{ 89, "VITA", "Vitality" };
constexpr TournamentTeamInfo GamerLegion{ 115, "GL", "GamerLegion" };
constexpr TournamentTeamInfo Heroic{ 95, "HERO", "Heroic" };
constexpr TournamentTeamInfo Apeks{ 117, "APEX", "Apeks" };
constexpr TournamentTeamInfo FaZe{ 61, "FAZE", "FaZe Clan" };
constexpr TournamentTeamInfo Monte{ 119, "MONT", "Monte" };
constexpr TournamentTeamInfo Liquid{ 48, "LIQ", "Team Liquid" };
constexpr TournamentTeamInfo IntoTheBreach{ 118, "ITB", "Into The Breach" };

// May 18-21 2023 in UTC
constexpr uint32_t May18 = 1684368000;
constexpr uint32_t May19 = May18 + 86400;
constexpr uint32_t May20 = May19 + 86400;
constexpr uint32_t May21 = May20 + 86400;
constexpr uint32_t Hour = 3600;
constexpr uint32_t MapLength = 4500;

// stage ids: quarterfinal maps 5-7, semifinal maps 8-10, grand final maps 11-13
constexpr uint64_t Id(uint32_t n)
{
    return (static_cast<uint64_t>(EventId) << 32) | n;
}

constexpr TournamentMatch Matches[] = {
    // quarterfinals
    { Id(1), May18 + 13 * Hour, 5, &Heroic, &FaZe, "de_nuke", 16, 14 },
    { Id(2), May18 + 13 * Hour + MapLength, 6, &Heroic, &FaZe, "de_overpass", 12, 16 },
    { Id(3), May18 + 13 * Hour + 2 * MapLength, 7, &Heroic, &FaZe, "de_mirage", 16, 6 },
    { Id(4), May18 + 17 * Hour, 5, &Vitality, &IntoTheBreach, "de_vertigo", 16, 11 },
    { Id(5), May18 + 17 * Hour + MapLength, 6, &Vitality, &IntoTheBreach, "de_anubis", 16, 12 },
    { Id(6), May19 + 13 * Hour, 5, &Monte, &GamerLegion, "de_mirage", 10, 16 },
    { Id(7), May19 + 13 * Hour + MapLength, 6, &Monte, &GamerLegion, "de_overpass", 3, 16 },
    { Id(8), May19 + 17 * Hour, 5, &Liquid, &Apeks, "de_ancient", 10, 16 },
    { Id(9), May19 + 17 * Hour + MapLength, 6, &Liquid, &Apeks, "de_overpass", 11, 16 },

    // semifinals
    { Id(10), May20 + 13 * Hour, 8, &Heroic, &GamerLegion, "de_ancient", 16, 13 },
    { Id(11), May20 + 13 * Hour + MapLength, 9, &Heroic, &GamerLegion, "de_inferno", 14, 16 },
    { Id(12), May20 + 13 * Hour + 2 * MapLength, 10, &Heroic, &GamerLegion, "de_mirage", 6, 16 },
    { Id(13), May20 + 17 * Hour, 8, &Apeks, &Vitality, "de_mirage", 14, 16 },
    { Id(14), May20 + 17 * Hour + MapLength, 9, &Apeks, &Vitality, "de_vertigo", 12, 16 },

    // grand final
    { Id(15), May21 + 15 * Hour, 11, &GamerLegion, &Vitality, "de_overpass", 6, 16 },
    { Id(16), May21 + 15 * Hour + MapLength, 12, &GamerLegion, &Vitality, "de_nuke", 13, 16 },
};

constexpr TournamentInfo Info{ EventId, "BLAST.tv Paris 2023 CS:GO Championship", Matches };

} // namespace Paris2023

const TournamentInfo *FindTournament(uint32_t eventId)
{
    if (eventId == Paris2023::EventId)
    {
        return &Paris2023::Info;
    }

    return nullptr;
}

const TournamentMatch *FindTournamentMatch(uint32_t eventId, uint64_t matchId)
{
    const TournamentInfo *tournament = FindTournament(eventId);
    if (!tournament)
    {
        return nullptr;
    }

    for (const TournamentMatch &match : tournament->matches)
    {
        if (match.matchId == matchId)
        {
            return &match;
        }
    }

    return nullptr;
}

static void SetAttribute(ItemDesc &desc, AttributeDefIndex defIndex, uint32_t value)
{
    for (ItemAttribute &attribute : desc.attributes)
    {
        if (attribute.DefIndex() == defIndex)
        {
            attribute.Value() = value;
            return;
        }
    }

    desc.attributes.emplace_back(defIndex, value);
}

void ApplySouvenirAttributes(const ItemSchema &itemSchema, const Item &package, ItemDesc &desc)
{
    desc.quality = Quality::Tournament;

    // souvenirs are never StatTrak
    std::erase_if(desc.attributes, [](const ItemAttribute &attribute)
        {
            return attribute.DefIndex() == AttributeDefIndex::KillEater
                || attribute.DefIndex() == AttributeDefIndex::KillEaterScoreType;
        });

    // the event is on the package definition, the rest was set when the package was redeemed
    const uint32_t eventId = package.HasAttribute(AttributeDefIndex::TournamentEventId)
        ? package.GetAttributeValue<uint32_t>(AttributeDefIndex::TournamentEventId)
        : itemSchema.GetItemAttributeUint32(package.DefIndex(), AttributeDefIndex::TournamentEventId).value_or(0);

    const uint32_t stageId = package.GetAttributeValue<uint32_t>(AttributeDefIndex::TournamentEventStageId);
    const uint32_t team0 = package.GetAttributeValue<uint32_t>(AttributeDefIndex::TournamentEventTeam0Id);
    const uint32_t team1 = package.GetAttributeValue<uint32_t>(AttributeDefIndex::TournamentEventTeam1Id);

    if (!eventId)
    {
        return;
    }

    SetAttribute(desc, AttributeDefIndex::TournamentEventId, eventId);

    if (stageId)
    {
        SetAttribute(desc, AttributeDefIndex::TournamentEventStageId, stageId);
    }

    // gold stickers: both teams, then the organiser (team 0). real souvenirs also had
    // the match MVP's autograph, which we don't have data for
    constexpr AttributeDefIndex StickerSlots[] = {
        AttributeDefIndex::StickerId0,
        AttributeDefIndex::StickerId1,
        AttributeDefIndex::StickerId2,
    };

    size_t slot = 0;
    auto addSticker = [&](std::optional<uint32_t> stickerKit)
    {
        if (stickerKit && slot < std::size(StickerSlots))
        {
            SetAttribute(desc, StickerSlots[slot++], *stickerKit);
        }
    };

    if (team0 && team1)
    {
        SetAttribute(desc, AttributeDefIndex::TournamentEventTeam0Id, team0);
        SetAttribute(desc, AttributeDefIndex::TournamentEventTeam1Id, team1);
        addSticker(itemSchema.FindTournamentGoldSticker(eventId, team0));
        addSticker(itemSchema.FindTournamentGoldSticker(eventId, team1));
    }

    addSticker(itemSchema.FindTournamentGoldSticker(eventId, 0));
}
