#include "stdafx.h"
#include "tournament.h"
#include "item_schema.h"
#include "random.h"

const TournamentInfo *FindTournament(uint32_t eventId)
{
    for (const TournamentInfo &tournament : AllTournaments())
    {
        if (tournament.eventId == eventId)
        {
            return &tournament;
        }
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

const TournamentMatch *RandomMatchForPackage(const ItemSchema &itemSchema, ItemDefIndex packageDefIndex, uint32_t eventId)
{
    const TournamentInfo *tournament = FindTournament(eventId);
    if (!tournament)
    {
        return nullptr;
    }

    // packages are named crate_<loc>_promo_<map>
    constexpr std::string_view PromoTag = "_promo_";
    std::string_view name = itemSchema.GetItemName(packageDefIndex);
    size_t promo = name.find(PromoTag);
    if (promo == std::string_view::npos)
    {
        return nullptr;
    }

    std::string_view map = name.substr(promo + PromoTag.size());

    std::vector<const TournamentMatch *> candidates;
    for (const TournamentMatch &match : tournament->matches)
    {
        if (match.map == map)
        {
            candidates.push_back(&match);
        }
    }

    return candidates.empty() ? nullptr : Random::Select(candidates);
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

    uint32_t stageId = package.GetAttributeValue<uint32_t>(AttributeDefIndex::TournamentEventStageId);
    uint32_t team0 = package.GetAttributeValue<uint32_t>(AttributeDefIndex::TournamentEventTeam0Id);
    uint32_t team1 = package.GetAttributeValue<uint32_t>(AttributeDefIndex::TournamentEventTeam1Id);

    if (!eventId)
    {
        return;
    }

    // teams came with the gold team stickers at DreamHack 2014, the souvenirs of the majors
    // before just have the event
    const bool hasTeams = !itemSchema.FindTournamentGoldTeams(eventId).empty();
    if (!hasTeams)
    {
        SetAttribute(desc, AttributeDefIndex::TournamentEventId, eventId);
        return;
    }

    // packages that weren't redeemed from a match (bought, or added with an editor) have no
    // teams, so pick one of the event's matches on the package's map
    if (!team0 || !team1)
    {
        const TournamentMatch *match = RandomMatchForPackage(itemSchema, package.DefIndex(), eventId);
        if (match)
        {
            stageId = match->stageId;
            team0 = match->team0->id;
            team1 = match->team1->id;
        }
    }

    // every package map has matches, this is for events we have no results for
    if (!team0 || !team1)
    {
        std::vector<uint32_t> teams = itemSchema.FindTournamentGoldTeams(eventId);
        if (teams.size() >= 2)
        {
            team0 = Random::Select(teams);
            std::erase(teams, team0);
            team1 = Random::Select(teams);
        }
    }

    SetAttribute(desc, AttributeDefIndex::TournamentEventId, eventId);

    if (stageId)
    {
        SetAttribute(desc, AttributeDefIndex::TournamentEventStageId, stageId);
    }

    // gold stickers like the real ones: both teams and the organiser since DreamHack 2014, plus
    // the autograph of the round's MVP since Cologne 2015. the majors before had none
    constexpr AttributeDefIndex StickerSlots[] = {
        AttributeDefIndex::StickerId0,
        AttributeDefIndex::StickerId1,
        AttributeDefIndex::StickerId2,
        AttributeDefIndex::StickerId3,
    };

    size_t slot = 0;
    auto addSticker = [&](std::optional<uint32_t> stickerKit)
    {
        if (stickerKit && slot < std::size(StickerSlots))
        {
            SetAttribute(desc, StickerSlots[slot++], *stickerKit);
        }
    };

    if (!team0 || !team1)
    {
        return;
    }

    SetAttribute(desc, AttributeDefIndex::TournamentEventTeam0Id, team0);
    SetAttribute(desc, AttributeDefIndex::TournamentEventTeam1Id, team1);
    addSticker(itemSchema.FindTournamentGoldSticker(eventId, team0));
    addSticker(itemSchema.FindTournamentGoldSticker(eventId, team1));
    addSticker(itemSchema.FindTournamentGoldSticker(eventId, 0));

    // the MVP was whoever was the round's MVP when the package dropped, so any of the ten players
    std::vector<ItemSchema::Autograph> players = itemSchema.FindTournamentGoldAutographs(eventId, team0);
    std::vector<ItemSchema::Autograph> players1 = itemSchema.FindTournamentGoldAutographs(eventId, team1);
    players.insert(players.end(), players1.begin(), players1.end());

    if (players.size())
    {
        const ItemSchema::Autograph &mvp = Random::Select(players);
        addSticker(mvp.stickerKit);
        SetAttribute(desc, AttributeDefIndex::TournamentMvpAccountId, mvp.playerAccountId);
    }
}
