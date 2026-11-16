// tests for item actions: StatTrak swap, the tournament coin graffiti choice and using items
// runs in a copy of tests/fixture (ctest does this), it writes csgo_gc/inventory.txt there

#include "stdafx.h"
#include "inventory.h"
#include "inventory_messages.h"
#include "keyvalue.h"

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

int main()
{
    WriteInventory();

    {
        Inventory inventory{ SteamId };

        // the game subscribes to the cache when it connects, before it can send any requests
        CMsgSOCacheSubscribed subscription;
        inventory.BuildCacheSubscription(subscription, 40, false);

        TestStatTrakSwap(inventory);
        TestCoinGraffiti(inventory);
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
