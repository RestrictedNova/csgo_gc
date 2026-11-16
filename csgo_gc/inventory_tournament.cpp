#include "stdafx.h"
#include "inventory.h"
#include "config.h"
#include "inventory_messages.h"
#include "inventory_modify.h"
#include "tournament.h"

// Tournament viewer passes. For an event held at <loc> (e.g. paris2023) the item schema has:
//
//   tournament_pass_<loc>             viewer pass, activates the event's coin
//   tournament_pass_<loc>_pack        viewer pass that also adds souvenir tokens
//   tournament_pass_<loc>_charge      souvenir token, added to an activated coin
//   tournament_journal_<loc>[_tier]   the coin, one definition per tier ordered by "upgrade level"
//
// all sharing the "tournament event id" attribute. Using any of them sends CMsgUseItem, and
// the client's popup waits for the ActivateFanToken customization notification with the
// coin's item id, then opens the coin.

constexpr std::string_view TournamentPassPrefix = "tournament_pass_";
constexpr std::string_view TournamentCoinPrefix = "tournament_journal_";

// "will pre-activate your coin with 3 Souvenir Tokens"
constexpr uint32_t PackSouvenirTokens = 3;

bool Inventory::IsTournamentPass(ItemDefIndex defIndex) const
{
    return m_itemSchema.GetItemName(defIndex).starts_with(TournamentPassPrefix);
}

std::vector<ItemDefIndex> Inventory::TournamentCoinTiers(uint32_t eventId) const
{
    std::vector<std::pair<uint32_t, ItemDefIndex>> tiers;

    for (ItemDefIndex defIndex : m_itemSchema.FindItemsByNamePrefix(TournamentCoinPrefix))
    {
        if (m_itemSchema.GetItemAttributeUint32(defIndex, AttributeDefIndex::TournamentEventId) == eventId)
        {
            uint32_t level = m_itemSchema.GetItemAttributeUint32(defIndex, AttributeDefIndex::UpgradeLevel).value_or(0);
            tiers.emplace_back(level, defIndex);
        }
    }

    std::sort(tiers.begin(), tiers.end());

    std::vector<ItemDefIndex> result;
    result.reserve(tiers.size());
    for (const auto &[level, defIndex] : tiers)
    {
        result.push_back(defIndex);
    }

    return result;
}

Item *Inventory::FindTournamentCoin(const std::vector<ItemDefIndex> &tiers)
{
    // the newest one, like the client picks
    Item *newest = nullptr;

    for (auto &[itemId, item] : m_items)
    {
        if (std::find(tiers.begin(), tiers.end(), item.DefIndex()) == tiers.end())
        {
            continue;
        }

        if (!newest || item.LowId() > newest->LowId())
        {
            newest = &item;
        }
    }

    return newest;
}

InventoryChangeMessages Inventory::UseTournamentPass(Item &pass)
{
    const ItemDefIndex passDefIndex = pass.DefIndex();
    const std::string_view name = m_itemSchema.GetItemName(passDefIndex);

    std::optional<uint32_t> eventId = m_itemSchema.GetItemAttributeUint32(passDefIndex, AttributeDefIndex::TournamentEventId);
    std::vector<ItemDefIndex> tiers = eventId ? TournamentCoinTiers(*eventId) : std::vector<ItemDefIndex>{};
    if (tiers.empty())
    {
        Platform::Print("No tournament coin for {}, ignoring\n", name);
        return {};
    }

    const bool isToken = name.ends_with("_charge");
    const bool isPack = name.ends_with("_pack");

    Item *coin = FindTournamentCoin(tiers);
    if (isToken && !coin)
    {
        Platform::Print("{} needs an activated viewer pass first, ignoring\n", name);
        return {};
    }

    if (!isToken && !isPack && coin)
    {
        Platform::Print("The viewer pass for event {} is already activated, ignoring\n", *eventId);
        return {};
    }

    const uint32_t tokens = isToken ? 1 : (isPack ? PackSouvenirTokens : 0);

    InventoryModify modify;

    if (!coin)
    {
        const ItemDefIndex coinDefIndex = tiers.front();

        ItemDesc desc{};
        if (!m_itemSchema.GetItemDesc(coinDefIndex, ItemOriginDropped, UnacknowledgedEarned, desc))
        {
            return {};
        }

        coin = &CreateItem(modify, std::move(desc));

        // the schema has these on the coin definition with force_gc_to_generate, and the client
        // reads them off the item (a missing token count hides the souvenir redeem button)
        std::optional<uint32_t> graffiti = m_itemSchema.GetItemAttributeUint32(coinDefIndex, AttributeDefIndex::StickerId0);
        if (graffiti)
        {
            modify.SetItemAttribute(*coin, AttributeDefIndex::StickerId0, *graffiti);
        }

        // bit 0 is the "activate the viewer pass" challenge
        uint32_t challenges = m_itemSchema.GetItemAttributeUint32(coinDefIndex, AttributeDefIndex::CampaignCompletionBitfield).value_or(1);
        modify.SetItemAttribute(*coin, AttributeDefIndex::CampaignCompletionBitfield, challenges);
        modify.SetItemAttribute(*coin, AttributeDefIndex::OperationDropsAwarded1, tokens);
        modify.SetItemAttribute(*coin, AttributeDefIndex::OperationDropsAwarded0, 0u);
    }
    else
    {
        uint32_t current = coin->GetAttributeValue<uint32_t>(AttributeDefIndex::OperationDropsAwarded1);
        modify.SetItemAttribute(*coin, AttributeDefIndex::OperationDropsAwarded1, current + tokens);
    }

    const uint64_t coinId = coin->FullIdFor(AccountId());

    if (GetConfig().m_destroyUsedItems)
    {
        DestroyItem(modify, &pass);
    }

    Platform::Print("Used {}, coin {} got {} souvenir token(s)\n", name, coinId, tokens);
    return FinishChanges(modify, k_EGCItemCustomizationNotification_ActivateFanToken, { coinId });
}

InventoryChangeMessages Inventory::RedeemSouvenir(uint64_t coinId, uint32_t eventId, const TournamentMatch &match, uint64_t &packageId)
{
    packageId = 0;

    Item *coin = FindItem(coinId);
    std::vector<ItemDefIndex> tiers = TournamentCoinTiers(eventId);
    if (!coin || std::find(tiers.begin(), tiers.end(), coin->DefIndex()) == tiers.end())
    {
        Platform::Print("Item {} is not a coin of event {}, ignoring\n", coinId, eventId);
        return {};
    }

    // same count as the client: the tier's free packages plus tokens added minus packages redeemed
    const uint32_t upgradeLevel = coin->HasAttribute(AttributeDefIndex::UpgradeLevel)
        ? coin->GetAttributeValue<uint32_t>(AttributeDefIndex::UpgradeLevel)
        : m_itemSchema.GetItemAttributeUint32(coin->DefIndex(), AttributeDefIndex::UpgradeLevel).value_or(0);
    const uint32_t added = coin->GetAttributeValue<uint32_t>(AttributeDefIndex::OperationDropsAwarded1);
    const uint32_t redeemed = coin->GetAttributeValue<uint32_t>(AttributeDefIndex::OperationDropsAwarded0);
    if (upgradeLevel + added <= redeemed)
    {
        Platform::Print("Coin {} has no souvenir tokens left, ignoring\n", coinId);
        return {};
    }

    // the event's souvenir package for this map, crate_<loc>_promo_<map>
    std::optional<ItemDefIndex> packageDefIndex;
    for (ItemDefIndex defIndex : m_itemSchema.FindItemsByNamePrefix("crate_"))
    {
        std::string_view name = m_itemSchema.GetItemName(defIndex);
        if (m_itemSchema.IsSouvenirPackage(defIndex)
            && m_itemSchema.GetItemAttributeUint32(defIndex, AttributeDefIndex::TournamentEventId) == eventId
            && name.size() > match.map.size()
            && name.ends_with(match.map)
            && name[name.size() - match.map.size() - 1] == '_')
        {
            packageDefIndex = defIndex;
            break;
        }
    }

    if (!packageDefIndex)
    {
        Platform::Print("No souvenir package for {} at event {}, ignoring\n", match.map, eventId);
        return {};
    }

    ItemDesc desc{};
    if (!m_itemSchema.GetItemDesc(*packageDefIndex, ItemOriginDropped, UnacknowledgedEarned, desc))
    {
        return {};
    }

    InventoryModify modify;
    Item &package = CreateItem(modify, std::move(desc));
    modify.SetItemAttribute(package, AttributeDefIndex::TournamentEventStageId, match.stageId);
    modify.SetItemAttribute(package, AttributeDefIndex::TournamentEventTeam0Id, match.team0->id);
    modify.SetItemAttribute(package, AttributeDefIndex::TournamentEventTeam1Id, match.team1->id);

    modify.SetItemAttribute(*coin, AttributeDefIndex::OperationDropsAwarded0, redeemed + 1);

    packageId = package.FullIdFor(AccountId());

    Platform::Print("Redeemed a {} souvenir package ({} vs {}) with coin {}\n",
        match.map, match.team0->tag, match.team1->tag, coinId);

    return FinishChanges(modify, k_EGCItemCustomizationNotification_GenerateSouvenir, { packageId });
}
