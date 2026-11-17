#include "stdafx.h"
#include "ui_patches.h"

#include <cctype>

namespace UIPatches
{

enum class PatchAction
{
    Remove, // the last piece
    InsertBefore, // text in front of the last piece
};

// a run of code to find, pieces separated by any whitespace (unused pieces left empty)
struct ScriptPatch
{
    std::string_view name;
    std::array<std::string_view, 3> pieces;
    unsigned group{ PatchGroupDisabledUI };
    PatchAction action{ PatchAction::Remove };
    std::string_view text{};

    constexpr size_t Count() const
    {
        size_t count = 0;
        while (count < pieces.size() && !pieces[count].empty())
        {
            count++;
        }

        return count;
    }
};

// the "Official Matchmaking" entry the final build commented out of the play menu's dropdown
constexpr std::string_view OfficialMatchmakingEntry = R"js(
	var elCsgoGcPlayType = $( '#PlayTopNavDropdown' );
	if ( elCsgoGcPlayType && !elCsgoGcPlayType.FindChildTraverse( 'Play-official' ) )
	{
		var elCsgoGcOfficial = $.CreatePanel( 'Label', elCsgoGcPlayType, 'Play-official' );
		elCsgoGcOfficial.text = $.Localize( '#play_setting_online' );
		elCsgoGcOfficial.SetAttributeString( 'data-type', 'official' );
		elCsgoGcPlayType.AddOption( elCsgoGcOfficial );
	}
	)js";

// csgo_gc has no matchmaking servers: accepting a match found by Official Matchmaking starts the
// map the gc picked on this machine, with the online competitive or wingman settings, and the game
// turns it into an official matchmaking server with bots once the player is in (MatchWatcher)
constexpr std::string_view AcceptStartsBotMatch = R"js(
		if ( m_lobbySettings && m_lobbySettings.options && m_lobbySettings.options.server === 'official' )
		{
			$.Schedule( 1.5, function()
			{
				var oCsgoGcSettings = LobbyAPI.GetSessionSettings();
				var oCsgoGcGame = ( oCsgoGcSettings && oCsgoGcSettings.game ) ? oCsgoGcSettings.game : {};
				var strCsgoGcMap = oCsgoGcGame.map ? oCsgoGcGame.map : '';
				var nCsgoGcMode = ( oCsgoGcGame.mode === 'scrimcomp2v2' ) ? 2 : 1;
				LobbyAPI.StopMatchmaking();
				$.DispatchEvent( 'CloseAcceptPopup' );
				$.DispatchEvent( 'UIPopupButtonClicked', '' );
				if ( strCsgoGcMap )
				{
					GameInterfaceAPI.ConsoleCommand( 'game_type 0; game_mode ' + nCsgoGcMode + '; map ' + strCsgoGcMap );
				}
			} );
		}
		)js";

constexpr ScriptPatch Patches[] = {
    // early returns Valve added in front of the original code in playercard.js
    {
        "player card rank, XP and service medal button",
        { "FindChildInLayoutFile( 'JsPlayerXp' );", "elRank.AddClass( 'hidden' );", "return;" },
    },
    {
        "player card skill groups",
        { "FindChildInLayoutFile( 'JsPlayerCardSkillGroupContainer' );", "elSkillGroupContainer.AddClass( 'hidden' );", "return;" },
    },

    // the "Legacy version of CS:GO" popup the final build shows in the main menu, CS:GO never had it
    {
        "main menu without the legacy version popup",
        { "_GcLogonNotificationReceived();", "_ShowLegacyVersionWarning();" },
    },

    // Official Matchmaking against bots (rank_progress), in mainmenu_play.js and popup_accept_match.js
    {
        "Official Matchmaking in the play menu",
        { "PlayMenu.Init();" },
        PatchGroupMatchmaking,
        PatchAction::InsertBefore,
        OfficialMatchmakingEntry,
    },
    {
        "matches found by Official Matchmaking against bots",
        { "LobbyAPI.SetLocalPlayerReady( 'accept' );", "}" },
        PatchGroupMatchmaking,
        PatchAction::InsertBefore,
        AcceptStartsBotMatch,
    },
};

static size_t SkipWhitespace(const std::string &text, size_t offset)
{
    while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset])))
    {
        offset++;
    }

    return offset;
}

// finds the pieces in order, returns the offset of the last one or npos
static size_t FindPatch(const std::string &source, const ScriptPatch &patch)
{
    for (size_t start = source.find(patch.pieces[0]); start != std::string::npos; start = source.find(patch.pieces[0], start + 1))
    {
        const size_t count = patch.Count();
        if (count == 1)
        {
            return start;
        }

        size_t offset = start + patch.pieces[0].size();
        bool matched = true;

        for (size_t i = 1; i < count; i++)
        {
            offset = SkipWhitespace(source, offset);
            if (source.compare(offset, patch.pieces[i].size(), patch.pieces[i]) != 0)
            {
                matched = false;
                break;
            }

            if (i + 1 < count)
            {
                offset += patch.pieces[i].size();
            }
        }

        if (matched)
        {
            return offset;
        }
    }

    return std::string::npos;
}

int Apply(std::string &source, unsigned groups)
{
    int applied = 0;

    for (const ScriptPatch &patch : Patches)
    {
        if (!(patch.group & groups))
        {
            continue;
        }

        size_t offset = FindPatch(source, patch);
        if (offset == std::string::npos)
        {
            continue;
        }

        if (patch.action == PatchAction::Remove)
        {
            source.erase(offset, patch.pieces[patch.Count() - 1].size());
        }
        else if (source.find(patch.text) == std::string::npos)
        {
            source.insert(offset, patch.text);
        }
        else
        {
            continue; // already there
        }

        applied++;

        // once per patch, scripts can get compiled again for every panel that uses them
        static std::unordered_set<std::string_view> s_logged;
        if (s_logged.insert(patch.name).second)
        {
            Platform::Print("Restored {}\n", patch.name);
        }
    }

    return applied;
}

} // namespace UIPatches

#ifdef _WIN32

#include <funchook.h>
#include <windows.h>
#include <winternl.h>

#include "config.h"

// just enough of the V8 ABI that v8.dll exports. v8::Local<T> is a single pointer and
// functions returning one take a hidden pointer for the result as their first argument
// (checked against the calls in panorama.dll)
namespace V8
{

using Local = void *;

struct Utf8Value
{
    char *str;
    int length;
    char reserved[8]; // in case the layout is bigger than we think
};

using Compile_t = Local *(__cdecl *)(Local *result, Local source, void *origin);
using NewFromUtf8_t = Local *(__cdecl *)(Local *result, void *isolate, const char *data, int type, int length);
using GetCurrentIsolate_t = void *(__cdecl *)();
using Utf8ValueConstruct_t = Utf8Value *(__thiscall *)(Utf8Value *self, Local value);
using Utf8ValueDestruct_t = void(__thiscall *)(Utf8Value *self);
using Utf8ValueGet_t = char *(__thiscall *)(Utf8Value *self);

constexpr int NormalString = 0; // v8::String::kNormalString

static NewFromUtf8_t NewFromUtf8;
static GetCurrentIsolate_t GetCurrentIsolate;
static Utf8ValueConstruct_t Utf8ValueConstruct;
static Utf8ValueDestruct_t Utf8ValueDestruct;
static Utf8ValueGet_t Utf8ValueGet;

// v8::Script::Compile(Local<String> source, ScriptOrigin *origin), what panorama.dll compiles with
static Compile_t Og_Compile;

} // namespace V8

// which patches the config asks for
static unsigned s_patchGroups;

static V8::Local *__cdecl Hk_Compile(V8::Local *result, V8::Local source, void *origin)
{
    std::string text;
    {
        V8::Utf8Value value{};
        V8::Utf8ValueConstruct(&value, source);
        const char *string = V8::Utf8ValueGet(&value);
        if (string)
        {
            text = string;
        }

        V8::Utf8ValueDestruct(&value);
    }

    if (UIPatches::Apply(text, s_patchGroups))
    {
        V8::Local patched{};
        V8::NewFromUtf8(&patched, V8::GetCurrentIsolate(), text.data(), V8::NormalString, static_cast<int>(text.size()));
        if (patched)
        {
            return V8::Og_Compile(result, patched, origin);
        }
    }

    return V8::Og_Compile(result, source, origin);
}

// the game loads v8.dll, unloads it and loads it again, so every load gets hooked
static HMODULE s_hookedV8;

static void HookV8(HMODULE v8)
{
    if (s_hookedV8 == v8)
    {
        return;
    }

    s_hookedV8 = v8;

    auto symbol = [v8](const char *name)
    {
        return reinterpret_cast<void *>(GetProcAddress(v8, name));
    };

    void *compile = symbol("?Compile@Script@v8@@SA?AV?$Local@VScript@v8@@@2@V?$Local@VString@v8@@@2@PAVScriptOrigin@2@@Z");
    V8::NewFromUtf8 = reinterpret_cast<V8::NewFromUtf8_t>(symbol("?NewFromUtf8@String@v8@@SA?AV?$Local@VString@v8@@@2@PAVIsolate@2@PBDW4NewStringType@12@H@Z"));
    V8::GetCurrentIsolate = reinterpret_cast<V8::GetCurrentIsolate_t>(symbol("?GetCurrent@Isolate@v8@@SAPAV12@XZ"));
    V8::Utf8ValueConstruct = reinterpret_cast<V8::Utf8ValueConstruct_t>(symbol("??0Utf8Value@String@v8@@QAE@V?$Local@VValue@v8@@@2@@Z"));
    V8::Utf8ValueDestruct = reinterpret_cast<V8::Utf8ValueDestruct_t>(symbol("??1Utf8Value@String@v8@@QAE@XZ"));
    V8::Utf8ValueGet = reinterpret_cast<V8::Utf8ValueGet_t>(symbol("??DUtf8Value@String@v8@@QAEPADXZ"));

    if (!compile || !V8::NewFromUtf8 || !V8::GetCurrentIsolate || !V8::Utf8ValueConstruct || !V8::Utf8ValueDestruct || !V8::Utf8ValueGet)
    {
        // a different game version, leave it alone
        Platform::Print("v8.dll is missing functions we need, not restoring disabled UI\n");
        return;
    }

    funchook_t *funchook = funchook_create();
    void *target = compile;
    if (!funchook
        || funchook_prepare(funchook, &target, reinterpret_cast<void *>(Hk_Compile)) != 0
        || funchook_install(funchook, 0) != 0)
    {
        Platform::Print("Hooking v8::Script::Compile failed, not restoring disabled UI\n");
        if (funchook)
        {
            funchook_destroy(funchook);
        }

        return;
    }

    V8::Og_Compile = reinterpret_cast<V8::Compile_t>(target);
}

// ntdll's dll load notifications (documented on MSDN, declared here since the SDK doesn't)
struct LdrDllLoadedNotificationData
{
    ULONG flags;
    const UNICODE_STRING *fullDllName;
    const UNICODE_STRING *baseDllName;
    void *dllBase;
    ULONG sizeOfImage;
};

constexpr ULONG LdrDllNotificationReasonLoaded = 1;
constexpr ULONG LdrDllNotificationReasonUnloaded = 2;

using LdrDllNotificationFunction_t = void(CALLBACK *)(ULONG reason, const LdrDllLoadedNotificationData *data, void *context);
using LdrRegisterDllNotification_t = NTSTATUS(NTAPI *)(ULONG flags, LdrDllNotificationFunction_t function, void *context, void **cookie);

static void CALLBACK OnDllNotification(ULONG reason, const LdrDllLoadedNotificationData *data, void *)
{
    if (!data->baseDllName)
    {
        return;
    }

    std::wstring_view name{ data->baseDllName->Buffer, data->baseDllName->Length / sizeof(wchar_t) };
    if (name.size() != 6 || _wcsnicmp(name.data(), L"v8.dll", 6) != 0)
    {
        return;
    }

    if (reason == LdrDllNotificationReasonLoaded)
    {
        HookV8(static_cast<HMODULE>(data->dllBase));
    }
    else if (reason == LdrDllNotificationReasonUnloaded && s_hookedV8 == data->dllBase)
    {
        // it can come back at the same address, unpatched
        s_hookedV8 = nullptr;
    }
}

void UIPatches::Install()
{
    s_patchGroups = (GetConfig().m_restoreDisabledUI ? PatchGroupDisabledUI : 0)
        | (GetConfig().m_rankProgress ? PatchGroupMatchmaking : 0);
    if (!s_patchGroups)
    {
        return;
    }

    // wait for the game to load v8.dll itself. loading it early ourselves made windows resolve its
    // icu dependencies from system32 instead of the game's bin directory, which breaks the game
    auto registerNotification = reinterpret_cast<LdrRegisterDllNotification_t>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));

    void *cookie;
    if (!registerNotification || registerNotification(0, OnDllNotification, nullptr, &cookie) != 0)
    {
        Platform::Print("Can't watch for v8.dll, not restoring disabled UI\n");
        return;
    }

    // in case it's already loaded
    HMODULE v8 = GetModuleHandleW(L"v8.dll");
    if (v8)
    {
        HookV8(v8);
    }
}

#else

void UIPatches::Install()
{
    // FIXME: same thing with libv8.so/dylib
}

#endif
