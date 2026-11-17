// tests for rank_progress: the skill rating, XP, levels, saving them and following a match against
// bots from its game events. runs in a copy of tests/fixture, it writes csgo_gc/progress.txt there

#include "stdafx.h"
#include "bot_matchmaking.h"
#include "end_of_match.h"
#include "match_tracker.h"
#include "platform.h"
#include "progress.h"
#include "reserved_server.h"

#include "cstrike15_usermessages.pb.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>

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

constexpr uint32_t Day = 24 * 60 * 60;
constexpr uint32_t Now = 1795000000; // november 2026

static MatchResult Match(int botDifficulty, MatchOutcome outcome, int won, int lost)
{
    MatchResult result;
    result.mode = RankedMode::Competitive;
    result.botDifficulty = botDifficulty;
    result.outcome = outcome;
    result.roundsWon = won;
    result.roundsLost = lost;
    return result;
}

static void TestRating()
{
    SkillRating start;

    SkillRating won = start;
    Progress::UpdateRating(won, Progress::BotRating(3), Progress::BotDeviation, 1);
    CHECK(won.rating > start.rating);
    CHECK(won.deviation < start.deviation);

    SkillRating lost = start;
    Progress::UpdateRating(lost, Progress::BotRating(3), Progress::BotDeviation, 0);
    CHECK(lost.rating < start.rating);

    // beating a bot you're far above is worth less than beating one above you
    SkillRating strong;
    strong.rating = 2200;
    strong.deviation = 60;
    SkillRating weak;
    weak.rating = 1500;
    weak.deviation = 60;
    const double strongBefore = strong.rating;
    const double weakBefore = weak.rating;
    Progress::UpdateRating(strong, Progress::BotRating(3), Progress::BotDeviation, 1);
    Progress::UpdateRating(weak, Progress::BotRating(3), Progress::BotDeviation, 1);
    CHECK(strong.rating - strongBefore < weak.rating - weakBefore);

    // a tie against an equal opponent changes nothing much
    SkillRating even;
    even.rating = Progress::BotRating(2);
    even.deviation = 60;
    Progress::UpdateRating(even, Progress::BotRating(2), Progress::BotDeviation, 0.5);
    CHECK(std::fabs(even.rating - Progress::BotRating(2)) < 1);
}

static void TestSkillGroups()
{
    CHECK(Progress::SkillGroupForRating(0) == RankSilver1);
    CHECK(Progress::SkillGroupForRating(1019) == RankSilver1);
    CHECK(Progress::SkillGroupForRating(1020) == RankSilver2);
    CHECK(Progress::SkillGroupForRating(2299) == RankSupremeMasterFirstClass);
    CHECK(Progress::SkillGroupForRating(2300) == RankGlobalElite);
    CHECK(Progress::SkillGroupForRating(5000) == RankGlobalElite);

    // winning every match: how far each bot difficulty can take you
    RankId ceilings[4];
    for (int difficulty = 0; difficulty < 4; difficulty++)
    {
        PlayerProgress progress;
        for (int i = 0; i < 300; i++)
        {
            progress.ApplyMatch(Match(difficulty, MatchOutcome::Win, 16, 5), Now);
        }

        ceilings[difficulty] = progress.SkillGroup(RankedMode::Competitive, Now);

        // beating easy bots afterwards doesn't take you down
        const double rating = progress.Skill(RankedMode::Competitive).rating;
        progress.ApplyMatch(Match(0, MatchOutcome::Win, 16, 0), Now);
        CHECK(progress.Skill(RankedMode::Competitive).rating >= rating);
    }

    CHECK(ceilings[0] == RankGoldNova2);
    CHECK(ceilings[1] == RankMasterGuardian2);
    CHECK(ceilings[2] == RankLegendaryEagle);
    CHECK(ceilings[3] == RankGlobalElite);
}

static void TestPlacementAndExpiry()
{
    PlayerProgress progress;
    CHECK(progress.SkillGroup(RankedMode::Competitive, Now) == RankNone);

    // losses don't count towards the 10 wins
    progress.ApplyMatch(Match(3, MatchOutcome::Loss, 10, 16), Now);
    progress.ApplyMatch(Match(3, MatchOutcome::Tie, 15, 15), Now);
    for (int i = 0; i < 9; i++)
    {
        progress.ApplyMatch(Match(3, MatchOutcome::Win, 16, 10), Now);
    }

    CHECK(progress.Skill(RankedMode::Competitive).wins == 9);
    CHECK(progress.SkillGroup(RankedMode::Competitive, Now) == RankNone);

    const PlayerProgress::MatchUpdate update = progress.ApplyMatch(Match(3, MatchOutcome::Win, 16, 10), Now);
    CHECK(update.oldRank == RankNone && update.newRank != RankNone);
    CHECK(progress.SkillGroup(RankedMode::Competitive, Now) == update.newRank);

    // wingman is separate
    CHECK(progress.SkillGroup(RankedMode::Wingman, Now) == RankNone);
    CHECK(progress.Skill(RankedMode::Wingman).wins == 0);

    // a month without a win hides it, the next win brings it back
    CHECK(progress.SkillGroup(RankedMode::Competitive, Now + 30 * Day) != RankNone);
    CHECK(progress.SkillGroup(RankedMode::Competitive, Now + 31 * Day) == RankNone);
    progress.ApplyMatch(Match(3, MatchOutcome::Win, 16, 2), Now + 31 * Day);
    CHECK(progress.SkillGroup(RankedMode::Competitive, Now + 31 * Day) != RankNone);
}

static void TestXp()
{
    CHECK(Progress::EarnedXp(RankedMode::Competitive, 16) == 480);
    CHECK(Progress::EarnedXp(RankedMode::Wingman, 9) == 135);

    // the weekly bonus tiers
    CHECK(Progress::ApplyWeeklyBonus(480, 0) == 1920);
    CHECK(Progress::ApplyWeeklyBonus(480, 5000) == 960);
    CHECK(Progress::ApplyWeeklyBonus(480, 8000) == 480);
    CHECK(Progress::ApplyWeeklyBonus(480, 20000) == 84);
    CHECK(Progress::ApplyWeeklyBonus(1000, 4000) == 500 + 875 * 2); // across a tier
    CHECK(Progress::ApplyWeeklyBonus(0, 0) == 0);

    // the same split the way the end of match screen lists it: earned, the bonus on top, and
    // earned at the reduced rate
    Progress::WeeklyXp split = Progress::SplitWeeklyXp(480, 0);
    CHECK(split.earned == 480 && split.bonus == 1440 && split.reduced == 0);
    split = Progress::SplitWeeklyXp(1000, 4000);
    CHECK(split.earned == 1000 && split.bonus == 375 + 875 && split.reduced == 0);
    split = Progress::SplitWeeklyXp(480, 8000);
    CHECK(split.earned == 480 && split.bonus == 0 && split.reduced == 0);
    split = Progress::SplitWeeklyXp(480, 20000);
    CHECK(split.earned == 0 && split.bonus == 0 && split.reduced == 84);
    split = Progress::SplitWeeklyXp(100, 11100); // 67 at 1x, the other 33 reduced
    CHECK(split.earned == 67 && split.bonus == 0 && split.reduced == 6);

    // weeks start on wednesday
    CHECK(Progress::WeekIndex(6 * Day - 1) != Progress::WeekIndex(6 * Day));
    CHECK(Progress::WeekIndex(6 * Day) == Progress::WeekIndex(13 * Day - 1));

    PlayerProgress progress;
    CHECK(progress.Level() == 1 && progress.Xp() == 0);

    PlayerProgress::MatchUpdate update = progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 8), Now);
    CHECK(update.earnedXp == 480 && update.gainedXp == 1920);
    CHECK(progress.Level() == 1 && progress.Xp() == 1920);

    // 1920 + 1920, then 660 fills the 4x tier (165 earned) and the other 315 earned count 2x:
    // 5130, level 2 with 130 XP
    progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 8), Now);
    update = progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 8), Now);
    CHECK(update.gainedXp == 660 + 2 * (480 - 165));
    CHECK(update.oldLevel == 1 && update.newLevel == 2 && progress.Xp() == 130);
    CHECK(update.oldXp == 3840 && update.wins == 3);

    // a new week has the full bonus again
    update = progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 8), Now + 7 * Day);
    CHECK(update.gainedXp == 1920);

    // leaving early: a loss, no XP
    MatchResult abandoned = Match(1, MatchOutcome::Win, 10, 3);
    abandoned.abandoned = true;
    const int xpBefore = progress.Xp();
    const int winsBefore = progress.Skill(RankedMode::Competitive).wins;
    update = progress.ApplyMatch(abandoned, Now + 7 * Day);
    CHECK(update.gainedXp == 0 && progress.Xp() == xpBefore);
    CHECK(progress.Skill(RankedMode::Competitive).wins == winsBefore);
    CHECK(update.newRating < update.oldRating);

    // the top level keeps a full bar until the service medal
    progress.SetLevel(Progress::MaxLevel, 4000);
    progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 0), Now + 14 * Day);
    CHECK(progress.Level() == Progress::MaxLevel && progress.Xp() == Progress::XpPerLevel - 1);
}

static void TestSaveLoad()
{
    Platform::RemoveFile("csgo_gc/progress.txt");

    PlayerProgress fresh;
    fresh.Load();
    CHECK(fresh.Level() == 1 && fresh.Xp() == 0);

    PlayerProgress progress;
    for (int i = 0; i < 12; i++)
    {
        progress.ApplyMatch(Match(2, MatchOutcome::Win, 16, 12), Now);
    }

    MatchResult wingman = Match(2, MatchOutcome::Win, 9, 4);
    wingman.mode = RankedMode::Wingman;
    progress.ApplyMatch(wingman, Now);
    progress.Save();

    PlayerProgress loaded;
    loaded.Load();
    CHECK(loaded.Level() == progress.Level() && loaded.Xp() == progress.Xp());
    CHECK(loaded.SkillGroup(RankedMode::Competitive, Now) == progress.SkillGroup(RankedMode::Competitive, Now));
    CHECK(loaded.Skill(RankedMode::Competitive).wins == 12);
    CHECK(loaded.Skill(RankedMode::Wingman).wins == 1);
    CHECK(std::fabs(loaded.Skill(RankedMode::Competitive).rating - progress.Skill(RankedMode::Competitive).rating) < 0.01);

    // the weekly bonus carries over too
    const PlayerProgress::MatchUpdate a = progress.ApplyMatch(Match(2, MatchOutcome::Win, 16, 0), Now);
    const PlayerProgress::MatchUpdate b = loaded.ApplyMatch(Match(2, MatchOutcome::Win, 16, 0), Now);
    CHECK(a.gainedXp == b.gainedXp);

    Platform::RemoveFile("csgo_gc/progress.txt");
}

class FakeEvent final : public MatchEvent
{
public:
    FakeEvent(std::string name, std::map<std::string, int> ints = {}, std::map<std::string, std::string> strings = {})
        : m_name{ std::move(name) }
        , m_ints{ std::move(ints) }
        , m_strings{ std::move(strings) }
    {
    }

    std::string_view Name() const override { return m_name; }

    int GetInt(const char *key) const override
    {
        auto it = m_ints.find(key);
        return it != m_ints.end() ? it->second : 0;
    }

    bool GetBool(const char *key) const override { return GetInt(key) != 0; }

    std::string_view GetString(const char *key) const override
    {
        auto it = m_strings.find(key);
        return it != m_strings.end() ? std::string_view{ it->second } : std::string_view{};
    }

private:
    std::string m_name;
    std::map<std::string, int> m_ints;
    std::map<std::string, std::string> m_strings;
};

constexpr int Human = 2;
constexpr int T = 2;
constexpr int CT = 3;

struct MatchFeed
{
    MatchTracker tracker;
    MatchSettings settings{ 0, 1, 3 }; // competitive, expert bots
    std::optional<MatchResult> result;

    void Send(const FakeEvent &event)
    {
        std::optional<MatchResult> r = tracker.OnEvent(event, [this] { return settings; });
        if (r)
        {
            result = r;
        }
    }

    // you and nine bots, you on CT
    void Join()
    {
        Send(FakeEvent{ "player_connect", { { "userid", Human } }, { { "networkid", "STEAM_1:0:1" } } });
        for (int bot = 3; bot < 12; bot++)
        {
            Send(FakeEvent{ "player_connect", { { "userid", bot } }, { { "networkid", "BOT" } } });
            Send(FakeEvent{ "player_team", { { "userid", bot }, { "team", bot < 7 ? CT : T }, { "isbot", 1 } } });
        }

        Send(FakeEvent{ "player_connect_full", { { "userid", Human } } });
        Send(FakeEvent{ "player_team", { { "userid", Human }, { "team", CT } } });
    }

    void Round(int humanTeam, int winner, int humanKills = 0)
    {
        Send(FakeEvent{ "player_spawn", { { "userid", Human }, { "teamnum", humanTeam } } });
        for (int i = 0; i < humanKills; i++)
        {
            Send(FakeEvent{ "player_death", { { "userid", 3 + i }, { "attacker", Human } } });
        }

        Send(FakeEvent{ "round_end", { { "winner", winner } } });
    }
};

static void TestTracker()
{
    // warmup doesn't count, then 16-10 with the sides switching at halftime
    {
        MatchFeed feed;
        feed.Join();
        feed.Round(CT, T); // warmup
        feed.Send(FakeEvent{ "begin_new_match" });

        for (int i = 0; i < 15; i++)
        {
            feed.Round(CT, i < 9 ? CT : T, 1); // 9-6 at the half
        }

        for (int i = 0; i < 11; i++)
        {
            feed.Round(T, i < 7 ? T : CT, 1); // then 7-4 on T: 16-10
        }

        feed.Send(FakeEvent{ "round_mvp", { { "userid", Human } } });
        feed.Send(FakeEvent{ "player_death", { { "userid", Human }, { "attacker", 5 } } });
        CHECK(!feed.result);
        feed.Send(FakeEvent{ "cs_win_panel_match" });

        CHECK(feed.result);
        if (feed.result)
        {
            CHECK(feed.result->mode == RankedMode::Competitive);
            CHECK(feed.result->botDifficulty == 3);
            CHECK(feed.result->outcome == MatchOutcome::Win && !feed.result->abandoned);
            CHECK(feed.result->roundsWon == 16 && feed.result->roundsLost == 10);
            CHECK(feed.result->kills == 26 && feed.result->deaths == 1 && feed.result->mvps == 1);
        }
    }

    // wingman, tied
    {
        MatchFeed feed;
        feed.settings = { 0, 2, 1 };
        feed.Join();
        feed.Send(FakeEvent{ "begin_new_match" });
        for (int i = 0; i < 16; i++)
        {
            feed.Round(CT, i % 2 ? CT : T);
        }

        feed.Send(FakeEvent{ "cs_win_panel_match" });
        CHECK(feed.result && feed.result->mode == RankedMode::Wingman && feed.result->outcome == MatchOutcome::Tie);
    }

    // casual and deathmatch aren't ranked
    {
        MatchFeed feed;
        feed.settings = { 0, 0, 3 };
        feed.Join();
        feed.Send(FakeEvent{ "begin_new_match" });
        feed.Round(CT, CT);
        feed.Send(FakeEvent{ "cs_win_panel_match" });
        CHECK(!feed.result);
    }

    // leaving is abandoning, a loss
    {
        MatchFeed feed;
        feed.Join();
        feed.Send(FakeEvent{ "begin_new_match" });
        feed.Round(CT, CT);
        feed.Send(FakeEvent{ "player_disconnect", { { "userid", Human } } });
        CHECK(feed.result && feed.result->abandoned && feed.result->outcome == MatchOutcome::Loss);
        CHECK(feed.result && feed.result->roundsWon == 1);
    }

    // so is the server going away mid match, but not after it ended
    {
        MatchFeed feed;
        feed.Join();
        feed.Send(FakeEvent{ "begin_new_match" });
        feed.Round(CT, CT);
        std::optional<MatchResult> gone = feed.tracker.OnServerGone();
        CHECK(gone && gone->abandoned);
        CHECK(!feed.tracker.OnServerGone());
    }

    // a bot leaving doesn't matter
    {
        MatchFeed feed;
        feed.Join();
        feed.Send(FakeEvent{ "begin_new_match" });
        feed.Send(FakeEvent{ "player_disconnect", { { "userid", 4 } } });
        CHECK(!feed.result);
    }

    // someone else joining your local server: not just you against bots
    {
        MatchFeed feed;
        feed.Join();
        feed.Send(FakeEvent{ "player_connect", { { "userid", 20 } }, { { "networkid", "STEAM_1:0:2" } } });
        feed.Send(FakeEvent{ "player_team", { { "userid", 20 }, { "team", T } } });
        feed.Send(FakeEvent{ "begin_new_match" });
        feed.Round(CT, CT);
        feed.Send(FakeEvent{ "cs_win_panel_match" });
        CHECK(!feed.result);
    }
}

static void TestBotMatchmaking()
{
    CHECK(BotMatchmaking::ModeFromSetting("competitive") == RankedMode::Competitive);
    CHECK(BotMatchmaking::ModeFromSetting("scrimcomp2v2") == RankedMode::Wingman);
    CHECK(!BotMatchmaking::ModeFromSetting("casual"));
    CHECK(!BotMatchmaking::ModeFromSetting("survival"));

    // the ticked maps from the game type: what the game sent with only mirage ticked, then
    // dust2, mirage and the lobby map veto
    std::vector<std::string> maps = BotMatchmaking::MapsFromGameType(0x8008, RankedMode::Competitive);
    CHECK((maps == std::vector<std::string>{ "de_mirage" }));
    maps = BotMatchmaking::MapsFromGameType(((0x2 | 0x80 | 0x20000) << 8) | 8, RankedMode::Competitive);
    CHECK((maps == std::vector<std::string>{ "de_dust2", "de_mirage" }));

    // the same bit is a different map in wingman
    maps = BotMatchmaking::MapsFromGameType((0x8000 << 8) | 8, RankedMode::Wingman);
    CHECK((maps == std::vector<std::string>{ "de_shortdust" }));

    // none ticked: the mode's own maps
    maps = BotMatchmaking::MapsFromGameType(8, RankedMode::Competitive);
    CHECK(maps.size() == 7 && std::ranges::count(maps, "de_mirage") == 1);
    maps = BotMatchmaking::MapsFromGameType((0x20000 << 8) | 8, RankedMode::Wingman);
    CHECK(std::ranges::count(maps, "de_shortdust") == 1);

    // bots a bit below your skill: a new account gets normal bots, the best get expert
    CHECK(BotMatchmaking::BotDifficultyFor(1500) == 1);
    CHECK(BotMatchmaking::BotDifficultyFor(900) == 0);
    CHECK(BotMatchmaking::BotDifficultyFor(1750) == 2);
    CHECK(BotMatchmaking::BotDifficultyFor(2300) == 3);

    // the reservation the game was tested with: the player's account id in hex at their place,
    // made up ones for the bots, half the list on each team
    CHECK(BotMatchmaking::Reservation(0x5f3a9c21, 0xb328643, RankedMode::Competitive, 7)
        == "Q5f3a9c21,1,1:[1][2][3][4][5][6][7][b328643][8][9]");
    CHECK(BotMatchmaking::Reservation(0xabc, 0xb328643, RankedMode::Wingman, 0) == "Qabc,1,1:[b328643][1][2][3]");

    // never the player's id twice
    const std::string low = BotMatchmaking::Reservation(1, 2, RankedMode::Wingman, 3);
    CHECK(low == "Q1,1,1:[1][3][4][2]");
}

static void TestReservationCheck()
{
    // the check the client sends the reserved server (engine.dll's CServerMsg_CheckReservation):
    // header, '!', host version, token, stage, reservation id, steam id
    const uint8_t packet[] = {
        0xff, 0xff, 0xff, 0xff, '!',
        0x39, 0x36, 0x00, 0x00, // 13881
        0x34, 0x12, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0xfb, 0xb5, 0x71, 0x5b, 0x89, 0x9f, 0xd7, 0x98,
        0x43, 0x86, 0x32, 0x0b, 0x01, 0x00, 0x10, 0x01,
    };

    const std::optional<ReservationCheck::Request> request = ReservationCheck::ParseRequest(packet, sizeof(packet));
    CHECK(request.has_value());
    if (request)
    {
        CHECK(request->hostVersion == 13881);
        CHECK(request->token == 0x1234);
        CHECK(request->stage == ReservationCheck::StageReserved);
        CHECK(request->reservationId == 0x98d79f895b71b5fbull);
        CHECK(request->steamId == 76561198148126275ull);

        // the server's answer (engine.dll writes it the same way): '%', the version, token and
        // stage back, then waiting for 0 of 10
        const std::vector<uint8_t> reply = ReservationCheck::Reply(*request, 0, 10);
        const std::vector<uint8_t> expected = {
            0xff, 0xff, 0xff, 0xff, '%',
            0x39, 0x36, 0x00, 0x00,
            0x34, 0x12, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x0a,
        };
        CHECK(reply == expected);
    }

    // anything else isn't a check
    CHECK(!ReservationCheck::ParseRequest(packet, sizeof(packet) - 1));
    uint8_t other[sizeof(packet)];
    memcpy(other, packet, sizeof(packet));
    other[4] = 'T';
    CHECK(!ReservationCheck::ParseRequest(other, sizeof(other)));
}

static void TestEndOfMatch()
{
    // a third win at level 1 with 3840 XP: 660 earned 4x then 315 earned 2x, a level up
    PlayerProgress progress;
    progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 8), Now);
    progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 8), Now);
    const PlayerProgress::MatchUpdate update = progress.ApplyMatch(Match(1, MatchOutcome::Win, 16, 8), Now);

    // the XP screen starts from the old level and XP and adds each reason
    CCSUsrMsg_XpUpdate xp;
    CHECK(xp.ParseFromString(EndOfMatch::XpUpdateMessage(0xb328643, update)));
    CHECK(xp.data().account_id() == 0xb328643);
    CHECK(xp.data().current_level() == 1 && xp.data().current_xp() == 3840);
    CHECK(xp.data().xp_progress_data_size() == 2);
    if (xp.data().xp_progress_data_size() == 2)
    {
        CHECK(xp.data().xp_progress_data(0).xp_category() == EndOfMatch::XpEarned);
        CHECK(xp.data().xp_progress_data(0).xp_points() == 480);
        CHECK(xp.data().xp_progress_data(1).xp_category() == EndOfMatch::XpWeeklyBonus);
        CHECK(xp.data().xp_progress_data(1).xp_points() == 495 + 315);
    }

    // the skill group screen: no skill group yet, 3 of the 10 wins
    CCSUsrMsg_ServerRankUpdate rank;
    CHECK(rank.ParseFromString(EndOfMatch::RankUpdateMessage(0xb328643, RankedMode::Competitive, update)));
    CHECK(rank.rank_update_size() == 1);
    if (rank.rank_update_size() == 1)
    {
        const CCSUsrMsg_ServerRankUpdate::RankUpdate &entry = rank.rank_update(0);
        CHECK(entry.account_id() == 0xb328643);
        CHECK(entry.rank_old() == 0 && entry.rank_new() == 0 && entry.num_wins() == 3);
        CHECK(entry.rank_type_id() == RankTypeCompetitive);
    }

    // a match with no rounds won lists no XP
    const PlayerProgress::MatchUpdate lost = progress.ApplyMatch(Match(1, MatchOutcome::Loss, 0, 16), Now);
    CHECK(xp.ParseFromString(EndOfMatch::XpUpdateMessage(0xb328643, lost)));
    CHECK(xp.data().xp_progress_data_size() == 0 && xp.data().current_xp() == 130);

    CHECK(rank.ParseFromString(EndOfMatch::RankUpdateMessage(0xb328643, RankedMode::Wingman, lost)));
    CHECK(rank.rank_update(0).rank_type_id() == RankTypeWingman);
}

int main()
{
    TestBotMatchmaking();
    TestReservationCheck();
    TestEndOfMatch();
    TestRating();
    TestSkillGroups();
    TestPlacementAndExpiry();
    TestXp();
    TestSaveLoad();
    TestTracker();

    if (s_failures)
    {
        std::printf("%d check(s) failed\n", s_failures);
        return 1;
    }

    std::printf("all checks passed\n");
    return 0;
}
