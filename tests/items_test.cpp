// tests for item actions: StatTrak swap, the tournament coin graffiti choice and using items
// runs in a copy of tests/fixture (ctest does this), it writes csgo_gc/inventory.txt there

#include "stdafx.h"
#include "inventory.h"
#include "inventory_messages.h"
#include "keyvalue.h"

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
        TestUseItem(inventory);
    }

    if (s_failures)
    {
        std::printf("%d check(s) failed\n", s_failures);
        return 1;
    }

    std::printf("all checks passed\n");
    return 0;
}
