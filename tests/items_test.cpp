// tests for item actions: StatTrak swap, the tournament coin graffiti choice and using items
// runs in a copy of tests/fixture (ctest does this), it writes csgo_gc/inventory.txt there

#include "stdafx.h"
#include "inventory.h"
#include "inventory_messages.h"
#include "item_schema.h"
#include "keyvalue.h"
#include "tournament.h"

#include "cstrike15_gcmessages.pb.h"
#include "econ_gcmessages.pb.h"
#include "gcsdk_gcmessages.pb.h"

#include <cstdio>

static int s_failures;

#define CHECK(condition)                                                          \
    do                                                                            \
    {                                                                             \
        if (!(condition))                                                         \
        {                                                                         \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            s_failures++;                                                         \
        }                                                                         \
    } while (0)

constexpr uint64_t SteamId = 76561197960265729;
constexpr uint32_t AccountId = SteamId & 0xffffffff;

// low item ids in the inventory written below
enum TestItem : uint32_t
{
    SwapTool = 1,
    StatTrakA = 2,
    StatTrakB = 3,
    PlainRifle = 4,
    BonusRank = 5,
    Coin = 6,
    M4A4 = 7, // equipped for CT in rifle slot 15
    M4A1S = 8,
    Agent = 9,
    MusicKit = 10,
    ViewerPass = 11,
    ViewerPassPack = 12,
    SouvenirToken = 13,
    SecondViewerPass = 14,
    StickeredRifle = 15, // sticker without a wear attribute, like souvenir stickers
    PatchedAgent = 16,
};

static uint64_t Id(TestItem item)
{
    return ComposeItemId(AccountId, item);
}

static void WriteInventory()
{
    FILE *f = fopen("csgo_gc/inventory.txt", "wb");
    fputs(R"("items"
{
    "1" { "inventory" "1" "def_index" "1324" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "1" }
    "2" { "inventory" "2" "def_index" "7" "level" "1" "quality" "9" "flags" "0" "origin" "8" "in_use" "0" "rarity" "3"
          "attributes" { "80" "100" "81" "0" } }
    "3" { "inventory" "3" "def_index" "7" "level" "1" "quality" "9" "flags" "0" "origin" "8" "in_use" "0" "rarity" "3"
          "attributes" { "80" "5" "81" "0" } }
    "4" { "inventory" "4" "def_index" "7" "level" "1" "quality" "4" "flags" "0" "origin" "8" "in_use" "0" "rarity" "3" }
    "5" { "inventory" "5" "def_index" "4607" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "1" }
    "6" { "inventory" "6" "def_index" "4874" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "1" }
    "7" { "inventory" "7" "def_index" "16" "level" "1" "quality" "4" "flags" "0" "origin" "8" "in_use" "0" "rarity" "3"
          "equipped_state" { "3" "15" } }
    "8" { "inventory" "8" "def_index" "60" "level" "1" "quality" "4" "flags" "0" "origin" "8" "in_use" "0" "rarity" "3" }
    "9" { "inventory" "9" "def_index" "5036" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "5" }
    "10" { "inventory" "10" "def_index" "1314" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "3" }
    "11" { "inventory" "11" "def_index" "4883" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "1" }
    "12" { "inventory" "12" "def_index" "4888" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "1" }
    "13" { "inventory" "13" "def_index" "4889" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "1" }
    "14" { "inventory" "14" "def_index" "4883" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "1" }
    "15" { "inventory" "15" "def_index" "7" "level" "1" "quality" "12" "flags" "0" "origin" "8" "in_use" "0" "rarity" "3"
           "attributes" { "113" "6623" } }
    "16" { "inventory" "16" "def_index" "5036" "level" "1" "quality" "4" "flags" "0" "origin" "0" "in_use" "0" "rarity" "5"
           "attributes" { "113" "4609" } }
}
)",
        f);
    fclose(f);
}

// reads an item back from the saved inventory.txt, nullptr if it no longer exists
struct SavedInventory
{
    SavedInventory()
        : read{ "csgo_gc/inventory.txt" }
    {
    }

    const KeyValue *Item(TestItem item) const
    {
        const KeyValue *items = read.Root().GetSubkey("items");
        return items ? items->GetSubkey(std::to_string(static_cast<uint32_t>(item))) : nullptr;
    }

    std::string_view Attribute(TestItem item, std::string_view defIndex) const
    {
        const KeyValue *saved = Item(item);
        const KeyValue *attributes = saved ? saved->GetSubkey("attributes") : nullptr;
        return attributes ? attributes->GetString(defIndex) : std::string_view{};
    }

    // the first saved item with this def index, and its attribute values
    const KeyValue *ItemWithDef(std::string_view defIndex) const
    {
        const KeyValue *items = read.Root().GetSubkey("items");
        if (items)
        {
            for (const KeyValue &item : items->Subkeys())
            {
                if (item.GetString("def_index") == defIndex)
                {
                    return &item;
                }
            }
        }

        return nullptr;
    }

    std::string_view AttributeOfDef(std::string_view defIndex, std::string_view attribute) const
    {
        const KeyValue *saved = ItemWithDef(defIndex);
        const KeyValue *attributes = saved ? saved->GetSubkey("attributes") : nullptr;
        return attributes ? attributes->GetString(attribute) : std::string_view{};
    }

    // the slot an item is equipped in for a class, empty if it isn't
    std::string_view EquippedSlot(TestItem item, std::string_view classId) const
    {
        const KeyValue *saved = Item(item);
        const KeyValue *equipped = saved ? saved->GetSubkey("equipped_state") : nullptr;
        return equipped ? equipped->GetString(classId) : std::string_view{};
    }

    KeyValueRead read;
};

static void TestStatTrakSwap(Inventory &inventory)
{
    // a plain item can't take part
    InventoryChangeMessages messages = inventory.StatTrakSwap(Id(SwapTool), Id(StatTrakA), Id(PlainRifle));
    CHECK(!messages.notification.has_request());

    // neither can the same item twice, or a tool that isn't the swap tool
    messages = inventory.StatTrakSwap(Id(SwapTool), Id(StatTrakA), Id(StatTrakA));
    CHECK(!messages.notification.has_request());
    messages = inventory.StatTrakSwap(Id(BonusRank), Id(StatTrakA), Id(StatTrakB));
    CHECK(!messages.notification.has_request());

    messages = inventory.StatTrakSwap(Id(SwapTool), Id(StatTrakA), Id(StatTrakB));
    CHECK(messages.notification.request() == k_EGCItemCustomizationNotification_StatTrakSwap);
    CHECK(messages.notification.item_id_size() == 2);
    CHECK(messages.client.updated.objects_modified_size() == 2);

    SavedInventory saved;
    CHECK(saved.Attribute(StatTrakA, "80") == "5");
    CHECK(saved.Attribute(StatTrakB, "80") == "100");
    CHECK(!saved.Item(SwapTool)); // used tools are destroyed by default
}

static void TestCoinGraffiti(Inventory &inventory)
{
    InventoryChangeMessages messages = inventory.ModifyItemAttribute(Id(Coin), 113, 42);
    CHECK(messages.client.updated.objects_modified_size() == 1);

    // only sticker slot 0 may be changed this way
    messages = inventory.ModifyItemAttribute(Id(Coin), 6, 1);
    CHECK(messages.client.updated.objects_modified_size() == 0);

    SavedInventory saved;
    CHECK(saved.Attribute(Coin, "113") == "42");
    CHECK(saved.Attribute(Coin, "6").empty());
}

static void TestShuffle(Inventory &inventory)
{
    CMsgAdjustItemEquippedStateMulti message;
    message.add_ct_equips(Id(M4A1S)); // shares the M4A4's slot
    message.add_t_equips(Id(Agent)); // agents have one slot
    message.add_noteam_equips(Id(MusicKit)); // so do music kits
    message.add_ct_equips(Id(PlainRifle)); // an AK has no slot for CT here, skipped

    InventoryChangeMessages messages = inventory.EquipItemsMulti(message);
    CHECK(messages.client.updated.objects_modified_size() >= 3);

    SavedInventory saved;
    CHECK(saved.EquippedSlot(M4A1S, "3") == "15");
    CHECK(saved.EquippedSlot(M4A4, "3").empty());
    CHECK(saved.EquippedSlot(Agent, "2") == "38");
    CHECK(saved.EquippedSlot(MusicKit, "0") == "54");
    CHECK(saved.EquippedSlot(PlainRifle, "3").empty());
}

static void TestTournamentPasses(Inventory &inventory)
{
    ItemDefIndex used;

    // a souvenir token needs an activated coin
    InventoryChangeMessages messages = inventory.UseItem(Id(SouvenirToken), used);
    CHECK(!messages.notification.has_request());
    CHECK(SavedInventory{}.Item(SouvenirToken));

    // the viewer pass creates the lowest tier coin and the client gets its id
    messages = inventory.UseItem(Id(ViewerPass), used);
    CHECK(messages.notification.request() == k_EGCItemCustomizationNotification_ActivateFanToken);
    CHECK(messages.client.created.size() == 1);
    {
        SavedInventory saved;
        const KeyValue *coin = saved.ItemWithDef("4884");
        CHECK(coin);
        CHECK(!saved.Item(ViewerPass));
        CHECK(saved.AttributeOfDef("4884", "237") == "0");
        CHECK(saved.AttributeOfDef("4884", "240") == "0");
        CHECK(saved.AttributeOfDef("4884", "185") == "1");
        CHECK(saved.AttributeOfDef("4884", "113") == "6732");
        if (coin && messages.notification.item_id_size() == 1)
        {
            CHECK(messages.notification.item_id(0) == ComposeItemId(AccountId, FromString<uint32_t>(coin->Name())));
        }
    }

    // a second viewer pass for the same event is refused
    messages = inventory.UseItem(Id(SecondViewerPass), used);
    CHECK(!messages.notification.has_request());

    // tokens add to the coin, one each or three from a pack
    messages = inventory.UseItem(Id(SouvenirToken), used);
    CHECK(messages.notification.request() == k_EGCItemCustomizationNotification_ActivateFanToken);
    CHECK(SavedInventory{}.AttributeOfDef("4884", "237") == "1");

    messages = inventory.UseItem(Id(ViewerPassPack), used);
    CHECK(messages.notification.request() == k_EGCItemCustomizationNotification_ActivateFanToken);
    SavedInventory saved;
    CHECK(saved.AttributeOfDef("4884", "237") == "4");
    CHECK(!saved.Item(SouvenirToken) && !saved.Item(ViewerPassPack));
}

static void TestSouvenirRedeeming(Inventory &inventory)
{
    // Apeks vs Vitality on Mirage, semifinal map 1
    const TournamentMatch *match = FindTournamentMatch(21, (21ull << 32) | 13);
    CHECK(match && match->map == "de_mirage" && match->stageId == 8);
    if (!match)
    {
        return;
    }

    const KeyValue *savedCoin = SavedInventory{}.ItemWithDef("4884");
    CHECK(savedCoin);
    if (!savedCoin)
    {
        return;
    }

    const uint64_t coinId = ComposeItemId(AccountId, FromString<uint32_t>(savedCoin->Name()));
    uint64_t packageId;

    // only coins of the event can redeem
    InventoryChangeMessages messages = inventory.RedeemSouvenir(Id(PlainRifle), 21, *match, packageId);
    CHECK(packageId == 0);

    messages = inventory.RedeemSouvenir(coinId, 21, *match, packageId);
    CHECK(packageId != 0);
    CHECK(messages.notification.request() == k_EGCItemCustomizationNotification_GenerateSouvenir);
    CHECK(messages.notification.item_id_size() == 1 && messages.notification.item_id(0) == packageId);
    {
        SavedInventory saved;
        CHECK(saved.AttributeOfDef("4894", "138") == "8");
        CHECK(saved.AttributeOfDef("4894", "139") == "117");
        CHECK(saved.AttributeOfDef("4894", "140") == "89");
        CHECK(saved.AttributeOfDef("4884", "240") == "1");
    }

    // the coin had 4 tokens, 3 are left and then it's out
    for (int i = 0; i < 3; i++)
    {
        inventory.RedeemSouvenir(coinId, 21, *match, packageId);
        CHECK(packageId != 0);
    }

    inventory.RedeemSouvenir(coinId, 21, *match, packageId);
    CHECK(packageId == 0);
}

static void TestSouvenirOpening()
{
    ItemSchema schema;

    // a redeemed Apeks vs Vitality package
    ItemDesc packageDesc{};
    packageDesc.defIndex = ToEnum<ItemDefIndex>(4894);
    packageDesc.quality = Quality::Unique;
    packageDesc.attributes.emplace_back(AttributeDefIndex::TournamentEventStageId, 8u);
    packageDesc.attributes.emplace_back(AttributeDefIndex::TournamentEventTeam0Id, 117u);
    packageDesc.attributes.emplace_back(AttributeDefIndex::TournamentEventTeam1Id, 89u);
    Item package{ 1, std::move(packageDesc) };

    // what case opening rolled from it, StatTrak included
    ItemDesc skin{};
    skin.defIndex = ToEnum<ItemDefIndex>(7);
    skin.quality = Quality::Strange;
    skin.attributes.emplace_back(AttributeDefIndex::TextureWear, 0.1f);
    skin.attributes.emplace_back(AttributeDefIndex::KillEater, 0u);
    skin.attributes.emplace_back(AttributeDefIndex::KillEaterScoreType, 0u);

    ApplySouvenirAttributes(schema, package, skin);

    auto value = [&](AttributeDefIndex defIndex) -> std::optional<uint32_t>
    {
        for (const ItemAttribute &attribute : skin.attributes)
        {
            if (attribute.DefIndex() == defIndex)
            {
                const uint32_t *result = std::get_if<uint32_t>(&attribute.Value());
                return result ? std::optional<uint32_t>{ *result } : std::nullopt;
            }
        }

        return std::nullopt;
    };

    CHECK(skin.quality == Quality::Tournament);
    CHECK(!value(AttributeDefIndex::KillEater) && !value(AttributeDefIndex::KillEaterScoreType));
    CHECK(value(AttributeDefIndex::TournamentEventId) == 21u);
    CHECK(value(AttributeDefIndex::TournamentEventStageId) == 8u);
    CHECK(value(AttributeDefIndex::TournamentEventTeam0Id) == 117u);
    CHECK(value(AttributeDefIndex::TournamentEventTeam1Id) == 89u);

    // gold team stickers, then the organiser; not the regular team sticker
    CHECK(value(AttributeDefIndex::StickerId0) == 6667u);
    CHECK(value(AttributeDefIndex::StickerId1) == 6623u);
    CHECK(value(AttributeDefIndex::StickerId2) == 6707u);

    // then the gold autograph of a player from either team as the MVP, never the regular autograph
    const std::optional<uint32_t> autograph = value(AttributeDefIndex::StickerId3);
    const std::optional<uint32_t> mvp = value(AttributeDefIndex::TournamentMvpAccountId);
    CHECK((autograph == 6800u && mvp == 153400465u) || (autograph == 6901u && mvp == 11111u));

    // a package that wasn't redeemed from a match (bought or added with an editor)
    // gets the teams of one of the event's matches on its map (Mirage)
    ItemDesc boughtDesc{};
    boughtDesc.defIndex = ToEnum<ItemDefIndex>(4894);
    boughtDesc.quality = Quality::Unique;
    Item bought{ 2, std::move(boughtDesc) };

    skin = ItemDesc{};
    skin.defIndex = ToEnum<ItemDefIndex>(7);
    skin.quality = Quality::Unique;
    ApplySouvenirAttributes(schema, bought, skin);

    const std::optional<uint32_t> team0 = value(AttributeDefIndex::TournamentEventTeam0Id);
    const std::optional<uint32_t> team1 = value(AttributeDefIndex::TournamentEventTeam1Id);
    bool playedOnMirage = false;
    for (const TournamentMatch &match : FindTournament(21)->matches)
    {
        playedOnMirage |= match.map == "de_mirage" && team0 == match.team0->id && team1 == match.team1->id
            && value(AttributeDefIndex::TournamentEventStageId) == match.stageId;
    }

    CHECK(playedOnMirage);

    // the organiser sticker is always there (team stickers depend on the fixture having them)
    bool hasOrganiser = false;
    for (AttributeDefIndex slot : { AttributeDefIndex::StickerId0, AttributeDefIndex::StickerId1, AttributeDefIndex::StickerId2 })
    {
        hasOrganiser |= value(slot) == 6707u;
    }

    CHECK(hasOrganiser);

    // a package from an early major: real souvenirs from those had no stickers (the event has no
    // gold ones), so only the event, no made up teams and none of its regular or holo stickers
    ItemDesc oldDesc{};
    oldDesc.defIndex = ToEnum<ItemDefIndex>(4200);
    oldDesc.quality = Quality::Unique;
    Item old{ 3, std::move(oldDesc) };

    skin = ItemDesc{};
    skin.defIndex = ToEnum<ItemDefIndex>(7);
    skin.quality = Quality::Unique;
    ApplySouvenirAttributes(schema, old, skin);

    CHECK(skin.quality == Quality::Tournament);
    CHECK(value(AttributeDefIndex::TournamentEventId) == 4u);
    CHECK(!value(AttributeDefIndex::TournamentEventTeam0Id) && !value(AttributeDefIndex::TournamentEventTeam1Id));
    CHECK(!value(AttributeDefIndex::StickerId0) && !value(AttributeDefIndex::StickerId1));
    CHECK(!value(AttributeDefIndex::StickerId2) && !value(AttributeDefIndex::StickerId3));
}

static void TestTournamentData()
{
    const TournamentInfo *paris = FindTournament(21);
    CHECK(paris && paris->matches.size() == 16);
    CHECK(!FindTournament(20));
    if (!paris)
    {
        return;
    }

    std::set<uint64_t> ids;
    for (const TournamentMatch &match : paris->matches)
    {
        CHECK(ids.insert(match.matchId).second);
        CHECK(match.team0->id && match.team1->id && match.team0 != match.team1);
        CHECK(std::max(match.score0, match.score1) == 16 && match.score0 != match.score1);
        CHECK(match.stageId >= 5 && match.stageId <= 13);
        CHECK(match.map.starts_with("de_"));
        CHECK(FindTournamentMatch(21, match.matchId) == &match);
    }
}

static void TestStickerScraping(Inventory &inventory)
{
    CMsgApplySticker scrape;
    scrape.set_item_item_id(Id(StickeredRifle));
    scrape.set_sticker_slot(0);

    // a sticker without a wear attribute is unworn: the first scrape wears it, it stays on
    InventoryChangeMessages messages = inventory.ScrapeSticker(scrape);
    CHECK(!messages.notification.has_request());
    {
        SavedInventory saved;
        CHECK(saved.Attribute(StickeredRifle, "113") == "6623");
        CHECK(saved.Attribute(StickeredRifle, "114") == "0.1");
    }

    // 10% more each time, and the tenth scrape removes it like in the real game
    int scrapes = 1;
    while (!messages.notification.has_request() && scrapes < 20)
    {
        messages = inventory.ScrapeSticker(scrape);
        scrapes++;

        if (scrapes == 9)
        {
            std::string_view wear = SavedInventory{}.Attribute(StickeredRifle, "114");
            CHECK(!wear.empty() && std::abs(FromString<float>(wear) - 0.9f) < 0.001f);
        }
    }

    CHECK(messages.notification.request() == k_EGCItemCustomizationNotification_RemoveSticker);
    CHECK(scrapes == 10);
    CHECK(SavedInventory{}.Attribute(StickeredRifle, "113").empty());

    // patches come off agents in one go
    scrape.set_item_item_id(Id(PatchedAgent));
    messages = inventory.ScrapeSticker(scrape);
    CHECK(messages.notification.request() == k_EGCItemCustomizationNotification_RemovePatch);
    CHECK(SavedInventory{}.Attribute(PatchedAgent, "113").empty());
}

static void TestUseItem(Inventory &inventory)
{
    ItemDefIndex used;

    // not usable: reported but nothing changes
    InventoryChangeMessages messages = inventory.UseItem(Id(PlainRifle), used);
    CHECK(used == ToEnum<ItemDefIndex>(7));
    CHECK(messages.client.destroyed.empty() && messages.client.created.empty());

    messages = inventory.UseItem(Id(BonusRank), used);
    CHECK(used == ItemDefIndex::BonusRank);
    CHECK(messages.client.destroyed.size() == 1);

    SavedInventory saved;
    CHECK(!saved.Item(BonusRank));
}

static const CMsgSOCacheSubscribed_SubscribedType *FindObjects(const CMsgSOCacheSubscribed &message, uint32_t type)
{
    for (const CMsgSOCacheSubscribed_SubscribedType &objects : message.objects())
    {
        if (objects.type_id() == type)
        {
            return &objects;
        }
    }

    return nullptr;
}

static void TestStatsSubscription(const CMsgSOCacheSubscribed &client, const CMsgSOCacheSubscribed &server)
{
    // the client gets an active subscription so the stats page opens
    const CMsgSOCacheSubscribed_SubscribedType *objects = FindObjects(client, SOTypeRecurringSubscription);
    CHECK(objects && objects->object_data_size() == 1);
    if (objects && objects->object_data_size() == 1)
    {
        CSOAccountRecurringSubscription subscription;
        CHECK(subscription.ParseFromString(objects->object_data(0)));
        CHECK(subscription.time_next_cycle() > static_cast<uint32_t>(time(nullptr)) + 24 * 60 * 60);
    }

    // the game server doesn't need it
    CHECK(!FindObjects(server, SOTypeRecurringSubscription));
}

int main()
{
    WriteInventory();

    {
        Inventory inventory{ SteamId };

        // the game subscribes to the cache when it connects, before it can send any requests
        CMsgSOCacheSubscribed subscription;
        inventory.BuildCacheSubscription(subscription, 40, false);

        CMsgSOCacheSubscribed serverSubscription;
        inventory.BuildCacheSubscription(serverSubscription, 40, true);
        TestStatsSubscription(subscription, serverSubscription);

        TestStatTrakSwap(inventory);
        TestCoinGraffiti(inventory);
        TestShuffle(inventory);
        TestTournamentPasses(inventory);
        TestSouvenirRedeeming(inventory);
        TestStickerScraping(inventory);
        TestUseItem(inventory);
    }

    TestSouvenirOpening();
    TestTournamentData();

    if (s_failures)
    {
        std::printf("%d check(s) failed\n", s_failures);
        return 1;
    }

    std::printf("all checks passed\n");
    return 0;
}
