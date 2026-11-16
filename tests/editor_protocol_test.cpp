// tests for the inventory editor wire format and float formatting
// must run from tests/fixture (ctest does this) so ItemSchema finds its files

// include the implementation to reach its file-local ParseItem and SerializeItem
#include "inventory_editor.cpp"

#include <cmath>
#include <cstdio>
#include <cstring>

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

static void Append(std::vector<uint8_t> &buffer, uint32_t value)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(value));
}

static void AppendAttribute(std::vector<uint8_t> &buffer, uint32_t defIndex, std::string_view value)
{
    Append(buffer, defIndex);
    Append(buffer, static_cast<uint32_t>(value.size()));
    buffer.insert(buffer.end(), value.begin(), value.end());
}

// item payload up to and including the attribute count
static std::vector<uint8_t> ItemHeader(uint32_t attributeCount)
{
    std::vector<uint8_t> buffer;
    Append(buffer, 1234); // item id (or request id for creation)
    for (int i = 0; i < 8; i++)
    {
        Append(buffer, 1); // inventory, def index, level, quality, flags, origin, in use, rarity
    }
    Append(buffer, attributeCount);
    return buffer;
}

static bool Parse(const std::vector<uint8_t> &data, EditorItem &item, ItemSchema &schema)
{
    MessageRead read{ data.data(), static_cast<uint32_t>(data.size()) };
    return ParseItem(read, item, schema) && read.IsValid() && read.IsDone();
}

template<typename T>
static const T *FindAttribute(const ItemDesc &desc, uint32_t defIndex)
{
    for (const ItemAttribute &attribute : desc.attributes)
    {
        if (FromEnum(attribute.DefIndex()) == defIndex)
        {
            return std::get_if<T>(&attribute.Value());
        }
    }

    return nullptr;
}

static bool SameBits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

static void TestParsesValidItem(ItemSchema &schema)
{
    std::vector<uint8_t> data = ItemHeader(4);
    AppendAttribute(data, 7, "661");
    AppendAttribute(data, 8, "1.23456e-05");
    AppendAttribute(data, 80, "500");
    AppendAttribute(data, 111, "Old Faithful");
    Append(data, 1); // equip count
    Append(data, 3);
    Append(data, 2);

    EditorItem item{};
    CHECK(Parse(data, item, schema));
    CHECK(item.lowId == 1234);
    CHECK(item.desc.attributes.size() == 4);
    CHECK(item.desc.equips.size() == 1);

    const float *seed = FindAttribute<float>(item.desc, 7);
    const float *wear = FindAttribute<float>(item.desc, 8);
    const uint32_t *kills = FindAttribute<uint32_t>(item.desc, 80);
    const std::string *name = FindAttribute<std::string>(item.desc, 111);
    CHECK(seed && *seed == 661.0f);
    CHECK(wear && SameBits(*wear, 1.23456e-05f));
    CHECK(kills && *kills == 500);
    CHECK(name && *name == "Old Faithful");
}

static void TestRejectsBadCounts(ItemSchema &schema)
{
    EditorItem item{};

    // reserving these used to fail and fast-fail the whole game
    std::vector<uint8_t> hugeAttributes = ItemHeader(0xFFFFFFFF);
    CHECK(!Parse(hugeAttributes, item, schema));

    std::vector<uint8_t> hugeEquips = ItemHeader(0);
    Append(hugeEquips, 0xFFFFFFFF);
    CHECK(!Parse(hugeEquips, item, schema));

    std::vector<uint8_t> tooFewBytes = ItemHeader(2);
    AppendAttribute(tooFewBytes, 7, "");
    CHECK(!Parse(tooFewBytes, item, schema));

    std::vector<uint8_t> truncated = ItemHeader(0);
    truncated.resize(20);
    CHECK(!Parse(truncated, item, schema));
}

static void TestFloatToString()
{
    CHECK(FloatToString(0.15f) == "0.15");
    CHECK(FloatToString(661.0f) == "661");

    // low wears are where the old 6 decimal formatting lost most of the value
    const float values[] = {
        0.0f, 1.0f, 0.15f, 0.07f, 1.23456e-05f, 3.0e-07f,
        std::nextafter(0.07f, 0.0f), std::nextafter(0.15f, 1.0f)
    };

    for (float value : values)
    {
        // the single argument FromString has the macOS fallback
        CHECK(SameBits(FromString<float>(FloatToString(value)), value));
    }
}

static void TestEditorRoundTripKeepsWear(ItemSchema &schema)
{
    const float wear = std::nextafter(0.07f, 0.0f); // just under minimal wear

    EditorItem original{};
    original.lowId = 99;
    original.desc.attributes.emplace_back(ToEnum<AttributeDefIndex>(8), wear);

    MessageWrite write;
    SerializeItem(write, original);
    std::vector<uint8_t> data = std::move(write).TakeBuffer();

    EditorItem parsed{};
    CHECK(Parse(data, parsed, schema));
    const float *parsedWear = FindAttribute<float>(parsed.desc, 8);
    CHECK(parsedWear && SameBits(*parsedWear, wear));
}

int main()
{
    ItemSchema schema;
    if (schema.GetAttributeType(ToEnum<AttributeDefIndex>(111)) != AttributeType::String)
    {
        std::printf("item schema fixture not loaded, run from tests/fixture\n");
        return 1;
    }

    TestParsesValidItem(schema);
    TestRejectsBadCounts(schema);
    TestFloatToString();
    TestEditorRoundTripKeepsWear(schema);

    if (s_failures)
    {
        std::printf("%d check(s) failed\n", s_failures);
        return 1;
    }

    std::printf("all checks passed\n");
    return 0;
}
