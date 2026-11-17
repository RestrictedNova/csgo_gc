#include "stdafx.h"
#include "bot_profiles.h"

#include <cmath>

namespace BotProfiles
{

// time to damage by skill group, ms. csst.at's averages of Leetify stats (November 2026) by
// FACEIT level, each skill group on the level its players usually reach: silver 1-2 (659, 646),
// gold nova 3-4 (631, 618), master guardian 5-7 (605, 596, 588), distinguished master guardian
// and legendary eagle 8-9 (581, 573), legendary eagle master 10 at 2000-2500 elo (561), supreme
// 10 at 2500-3000 (545), global elite 10 above 3000 (526 to 470). The pros' median is 452
constexpr double TimesToDamage[] = {
    680, 670, 662, 655, 648, 640, // silver I to silver elite master
    631, 625, 618, 611, // gold nova I to master
    605, 596, 588, // master guardian I, II, elite
    581, 573, 561, // distinguished master guardian, legendary eagle (master)
    545, 515, // supreme master first class, the global elite
};

static int TierIndex(RankId tier)
{
    return std::clamp(static_cast<int>(tier), static_cast<int>(RankSilver1), static_cast<int>(RankGlobalElite))
        - static_cast<int>(RankSilver1);
}

RankId TierFor(double rating)
{
    return Progress::SkillGroupForRating(rating);
}

std::string_view TierName(RankId tier)
{
    constexpr std::string_view Names[] = {
        "Silver I", "Silver II", "Silver III", "Silver IV", "Silver Elite", "Silver Elite Master",
        "Gold Nova I", "Gold Nova II", "Gold Nova III", "Gold Nova Master",
        "Master Guardian I", "Master Guardian II", "Master Guardian Elite", "Distinguished Master Guardian",
        "Legendary Eagle", "Legendary Eagle Master", "Supreme Master First Class", "The Global Elite",
    };

    return Names[TierIndex(tier)];
}

double TierRating(RankId tier)
{
    const RankId group = static_cast<RankId>(TierIndex(tier) + RankSilver1);
    return Progress::SkillGroupFloor(group) + 40;
}

double TimeToDamage(RankId tier)
{
    return TimesToDamage[TierIndex(tier)];
}

double Strength(RankId tier)
{
    const double slowest = TimesToDamage[0];
    const double fastest = TimesToDamage[std::size(TimesToDamage) - 1];
    return (slowest - TimeToDamage(tier)) / (slowest - fastest);
}

// a value from the silver I bots' to the global elite bots', the gaps between skill groups the
// gaps between their players' times to damage. Silver I bots are about Valve's normal bots, the
// global elite about its best (elite) ones, which are the hardest bots CS:GO had
static double Scale(double silver, double global, double strength)
{
    return std::lerp(silver, global, strength);
}

Settings SettingsFor(RankId tier)
{
    const double s = Strength(tier);

    Settings settings;
    settings.reactionTime = Scale(0.45, 0.06, s);
    settings.attackDelay = Scale(0.5, 0, s);
    settings.aimFocusInitial = 16 * std::pow(0.5 / 16, s); // 16 degrees down to half a degree
    settings.aimFocusDecay = Scale(0.65, 0.1, s);
    settings.aimFocusOffsetScale = Scale(0.55, 0.05, s);
    settings.aimFocusInterval = Scale(0.7, 0.05, s);
    settings.skill = static_cast<int>(std::lround(Scale(30, 100, s)));
    settings.aggression = static_cast<int>(std::lround(Scale(35, 85, s)));
    settings.teamwork = static_cast<int>(std::lround(Scale(55, 95, s)));
    settings.lookAngleMaxAccelAttacking = Scale(2500, 6000, s);
    settings.lookAngleStiffnessAttacking = Scale(120, 260, s);
    settings.lookAngleDampingAttacking = Scale(28, 38, s);
    return settings;
}

// Valve's names from botprofile.db, a different set of them for every skill group
constexpr std::string_view Names[] = {
    "Cliffe", "Minh", "Crusher", "Garret", "Rock", "Shark", "Wolf", "Gunner",
    "Vitaliy", "Ridgway", "Steel", "Stone", "Arnold", "Brett", "Kurt", "Kyle",
    "Moe", "Quade", "Quintin", "Ringo", "Rip", "Zach", "Cory", "Quinn",
    "Seth", "Vinny", "Brian", "Chad", "Chet", "Gabe", "Hank", "Ivan",
    "Jim", "Joe", "John", "Tony", "Tyler", "Victor", "Vladimir", "Zane",
    "Zim", "Adrian", "Albert", "Alfred", "Allen", "Andy", "Bert", "Bill",
};

// mostly riflers, a few awpers, Valve's weapon templates
constexpr std::string_view Weapons[] = {
    "Rifle", "RifleT", "Rifle", "RifleT", "Sniper", "Rifle", "RifleT", "Punch",
    "Rifle", "RifleT", "PunchT", "Rifle", "Sniper", "RifleT", "Rifle", "Spray",
    "RifleT", "Punch", "Rifle", "PunchT", "Sniper", "RifleT", "Rifle", "Shotgun",
};

// Valve's botprofile.db, everything before its own profiles
constexpr std::string_view DefaultsAndWeapons = R"(Default
	Skill = 50
	Aggression = 50
	ReactionTime = 0.3
	AttackDelay = 0
	Teamwork = 75
	AimFocusInitial = 20
	AimFocusDecay = 0.7
	AimFocusOffsetScale = 0.30
	AimfocusInterval = 0.8
	WeaponPreference = none
	Cost = 0
	Difficulty = NORMAL
	VoicePitch = 100
	Skin = 0
	LookAngleMaxAccelNormal = 2000.0
	LookAngleStiffnessNormal = 100.0
	LookAngleDampingNormal = 25.0
	LookAngleMaxAccelAttacking = 3000.0
	LookAngleStiffnessAttacking = 150.0
	LookAngleDampingAttacking = 30.0
End

Template Rifle
	WeaponPreference = m4a1
	WeaponPreference = ak47
	WeaponPreference = famas
	WeaponPreference = galilar
	WeaponPreference = mp7
End

Template RifleT
	WeaponPreference = ak47
	WeaponPreference = m4a1
	WeaponPreference = galilar
	WeaponPreference = famas
	WeaponPreference = mp7
End

Template Punch
	WeaponPreference = aug
	WeaponPreference = sg556
	WeaponPreference = famas
	WeaponPreference = galilar
	WeaponPreference = mp7
End

Template PunchT
	WeaponPreference = aug
	WeaponPreference = sg556
	WeaponPreference = famas
	WeaponPreference = galilar
	WeaponPreference = mp7
End

Template Sniper
	WeaponPreference = awp
	WeaponPreference = scar20
	WeaponPreference = g3sg1
	WeaponPreference = ssg08
	WeaponPreference = famas
	WeaponPreference = galilar
	WeaponPreference = mp7
End

Template Shotgun
	WeaponPreference = xm1014
	WeaponPreference = nova
	WeaponPreference = famas
	WeaponPreference = galilar
	WeaponPreference = mp7
End

Template Spray
	WeaponPreference = p90
	WeaponPreference = mp9
	WeaponPreference = mac10
	WeaponPreference = mp7
End

)";

std::string Database(RankId tier)
{
    const Settings settings = SettingsFor(tier);

    std::string text = std::format("// csgo_gc's bots for Official Matchmaking: players of skill group {} (time to damage "
                                   "{:.0f} ms), generated\n\n",
        TierIndex(tier) + 1, TimeToDamage(tier));
    text += DefaultsAndWeapons;

    text += std::format(R"(Template Tier
	Skill = {}
	Aggression = {}
	ReactionTime = {:.3f}
	AttackDelay = {:.3f}
	Teamwork = {}
	AimFocusInitial = {:.3f}
	AimFocusDecay = {:.3f}
	AimFocusOffsetScale = {:.3f}
	AimfocusInterval = {:.3f}
	Difficulty = EXPERT
	LookAngleMaxAccelAttacking = {:.1f}
	LookAngleStiffnessAttacking = {:.1f}
	LookAngleDampingAttacking = {:.1f}
End

)",
        settings.skill, settings.aggression, settings.reactionTime, settings.attackDelay, settings.teamwork,
        settings.aimFocusInitial, settings.aimFocusDecay, settings.aimFocusOffsetScale, settings.aimFocusInterval,
        settings.lookAngleMaxAccelAttacking, settings.lookAngleStiffnessAttacking, settings.lookAngleDampingAttacking);

    // some bots more aggressive than others, all of them as good
    const size_t firstName = static_cast<size_t>(TierIndex(tier)) * 7;
    for (size_t i = 0; i < std::size(Weapons); i++)
    {
        const int aggression = std::clamp(settings.aggression + static_cast<int>((i * 7) % 31) - 15, 0, 100);
        const int pitch = 85 + static_cast<int>((i * 13) % 31);
        text += std::format("Tier+{} {}\n\tAggression = {}\n\tVoicePitch = {}\nEnd\n\n", Weapons[i],
            Names[(firstName + i) % std::size(Names)], aggression, pitch);
    }

    return text;
}

} // namespace BotProfiles
