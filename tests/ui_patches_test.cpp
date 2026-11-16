// tests for the Panorama script patches that bring back disabled UI
// pass a path to a real playercard.js to see what gets patched in it

#include "stdafx.h"
#include "ui_patches.h"

#include <cstdio>
#include <fstream>
#include <sstream>

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

static size_t Count(const std::string &text, std::string_view what)
{
    size_t count = 0;
    for (size_t i = text.find(what); i != std::string::npos; i = text.find(what, i + 1))
    {
        count++;
    }

    return count;
}

// shaped like the final build's playercard.js: CRLF, tabs, an early return in front of the
// original code, which has conditional returns of its own that have to stay
static const char PlayerCard[] =
    "\tvar _SetRank = function()\r\n"
    "\t{\r\n"
    "\t\tvar elRank = $.GetContextPanel().FindChildInLayoutFile( 'JsPlayerXp' );\r\n"
    "\t\telRank.AddClass( 'hidden' );\r\n"
    "\t\treturn;\r\n"
    "\r\n"
    "\t\t\r\n"
    "\t\tif ( !MyPersonaAPI.IsInventoryValid() )\r\n"
    "\t\t{\r\n"
    "\t\t\telRank.AddClass( 'hidden' );\r\n"
    "\t\t\treturn;\r\n"
    "\t\t}\r\n"
    "\t\telRank.RemoveClass( 'hidden' );\r\n"
    "\t};\r\n"
    "\tvar _SetAllSkillGroups = function()\r\n"
    "\t{\r\n"
    "\t\tvar elSkillGroupContainer = $.GetContextPanel().FindChildInLayoutFile( 'JsPlayerCardSkillGroupContainer' );\r\n"
    "\t\telSkillGroupContainer.AddClass( 'hidden' );\r\n"
    "\t\treturn;\r\n"
    "\t\t\r\n"
    "\t\tif ( !_IsPlayerPrime() )\r\n"
    "\t\t{\r\n"
    "\t\t\telSkillGroupContainer.AddClass( 'hidden' );\r\n"
    "\t\t\treturn;\r\n"
    "\t\t}\r\n"
    "\t\telSkillGroupContainer.RemoveClass( 'hidden' );\r\n"
    "\t};\r\n";

static void TestPlayerCard()
{
    std::string source = PlayerCard;
    CHECK(Count(source, "return;") == 4);

    CHECK(UIPatches::Apply(source) == 2);

    // only the two early returns are gone, everything else is untouched
    CHECK(Count(source, "return;") == 2);
    CHECK(source.find("elRank.AddClass( 'hidden' );\r\n\t\t\r\n") != std::string::npos);
    CHECK(source.find("\t\t\telRank.AddClass( 'hidden' );\r\n\t\t\treturn;") != std::string::npos);
    CHECK(source.find("\t\t\telSkillGroupContainer.AddClass( 'hidden' );\r\n\t\t\treturn;") != std::string::npos);
    CHECK(source.size() == std::string_view{ PlayerCard }.size() - 2 * std::string_view{ "return;" }.size());

    // patched scripts stay as they are
    std::string again = source;
    CHECK(UIPatches::Apply(again) == 0);
    CHECK(again == source);
}

// shaped like the final build's mainmenu.js
static const char MainMenu[] =
    "\t\t_GcLogonNotificationReceived();\r\n"
    "\t\t_ShowLegacyVersionWarning();\r\n"
    "\t\t_BetaEnrollmentStatusChange();\r\n"
    "\t};\r\n"
    "\tvar _ShowLegacyVersionWarning = function ()\r\n"
    "\t{\r\n"
    "\t\tUiToolkitAPI.ShowGenericPopupTwoOptions( '#legacy_support_text_title' );\r\n"
    "\t}\r\n";

static void TestLegacyPopup()
{
    std::string source = MainMenu;
    CHECK(UIPatches::Apply(source) == 1);

    // the call is gone, the function and the calls around it stay
    CHECK(Count(source, "_ShowLegacyVersionWarning();") == 0);
    CHECK(source.find("var _ShowLegacyVersionWarning = function ()") != std::string::npos);
    CHECK(source.find("_GcLogonNotificationReceived();") != std::string::npos);
    CHECK(source.find("_BetaEnrollmentStatusChange();") != std::string::npos);
}

static void TestOtherScripts()
{
    std::string source = "var x = function()\r\n{\r\n\tel.AddClass( 'hidden' );\r\n\treturn;\r\n\tfoo();\r\n};\r\n";
    const std::string original = source;
    CHECK(UIPatches::Apply(source) == 0);
    CHECK(source == original);
}

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        std::ifstream file{ argv[1], std::ios::binary };
        std::stringstream contents;
        contents << file.rdbuf();
        std::string source = contents.str();
        std::printf("%d patch(es) applied to %s\n", UIPatches::Apply(source), argv[1]);
        return 0;
    }

    TestPlayerCard();
    TestLegacyPopup();
    TestOtherScripts();

    if (s_failures)
    {
        std::printf("%d check(s) failed\n", s_failures);
        return 1;
    }

    std::printf("all checks passed\n");
    return 0;
}
