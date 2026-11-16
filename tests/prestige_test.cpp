// tests for service medals (prestige coins): the schema lookup and the claim flow
// runs in a copy of tests/fixture (ctest does this), it writes csgo_gc/inventory.txt there

#include "stdafx.h"
#include "inventory.h"
#include "inventory_messages.h"

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

static ItemDefIndex Def(uint32_t value)
{
    return ToEnum<ItemDefIndex>(value);
}

static void TestSchema()
{
    ItemSchema schema;
    CHECK(schema.LatestPrestigeYear() == 2023);

    // sorted lowest tier first even though the fixture lists them out of order
    const std::vector<ItemDefIndex> *coins = schema.GetPrestigeCoins(2023);
    CHECK(coins && *coins == std::vector<ItemDefIndex>({ Def(4873), Def(4874), Def(4875) }));
    CHECK(schema.GetPrestigeCoins(2022) && schema.GetPrestigeCoins(2022)->size() == 2);
    CHECK(!schema.GetPrestigeCoins(2019));
}

static void TestClaimFlow()
{
    // start from an empty inventory every run
    std::remove("csgo_gc/inventory.txt");

    Inventory inventory{ SteamId };
    ItemDefIndex offer;
    uint64_t upgradeItemId;
    uint64_t coinItemId;

    // the game subscribes to the cache when it connects, before it can send any requests
    CMsgSOCacheSubscribed subscription;
    inventory.BuildCacheSubscription(subscription, 40, false);

    // nothing owned: offer the first tier of the newest year
    CHECK(inventory.GetPrestigeCoinOffer(offer, upgradeItemId));
    CHECK(offer == Def(4873) && upgradeItemId == 0);

    // claiming anything other than the offer does nothing
    InventoryChangeMessages messages = inventory.ClaimPrestigeCoin(Def(4874), coinItemId);
    CHECK(coinItemId == 0);
    CHECK(messages.client.created.empty());

    // claiming the offer creates the medal
    messages = inventory.ClaimPrestigeCoin(Def(4873), coinItemId);
    CHECK(coinItemId != 0);
    CHECK(messages.client.created.size() == 1);
    const uint64_t medalId = coinItemId;

    // next time the same item is upgraded, keeping its id
    CHECK(inventory.GetPrestigeCoinOffer(offer, upgradeItemId));
    CHECK(offer == Def(4874) && upgradeItemId == medalId);
    messages = inventory.ClaimPrestigeCoin(Def(4874), coinItemId);
    CHECK(coinItemId == medalId);
    CHECK(messages.client.created.empty());
    CHECK(messages.client.updated.objects_modified_size() == 1);

    messages = inventory.ClaimPrestigeCoin(Def(4875), coinItemId);
    CHECK(coinItemId == medalId);

    // at the top tier the same medal is offered again and claiming it changes nothing
    CHECK(inventory.GetPrestigeCoinOffer(offer, upgradeItemId));
    CHECK(offer == Def(4875) && upgradeItemId == medalId);
    messages = inventory.ClaimPrestigeCoin(Def(4875), coinItemId);
    CHECK(coinItemId == medalId);
    CHECK(messages.client.created.empty());
    CHECK(messages.client.updated.objects_modified_size() == 0);

    // the level update goes to the client as a persona data change
    messages = inventory.UpdatePlayerLevel(1);
    CHECK(messages.client.updated.objects_modified_size() == 1);
    CHECK(messages.client.updated.objects_modified(0).type_id() == SOTypePersonaDataPublic);
}

static void TestUpgradeSurvivesRestart()
{
    // the inventory from TestClaimFlow was saved on destruction, the medal is still the top tier
    Inventory inventory{ SteamId };
    ItemDefIndex offer;
    uint64_t upgradeItemId;
    CHECK(inventory.GetPrestigeCoinOffer(offer, upgradeItemId));
    CHECK(offer == Def(4875) && upgradeItemId != 0);
}

int main()
{
    TestSchema();
    TestClaimFlow();
    TestUpgradeSurvivesRestart();

    if (s_failures)
    {
        std::printf("%d check(s) failed\n", s_failures);
        return 1;
    }

    std::printf("all checks passed\n");
    return 0;
}
