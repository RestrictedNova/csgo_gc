#include "stdafx.h"
#include "platform.h"
#include "config.h"
#include "steam_hook.h"
#include "ui_patches.h"

void InstallGC(bool dedicated)
{
    Platform::Initialize();
    LoadConfig();
    SteamHookInstall(dedicated);

    // before the game loads panorama, dedicated servers don't have one
    if (!dedicated)
    {
        UIPatches::Install();
    }
}
