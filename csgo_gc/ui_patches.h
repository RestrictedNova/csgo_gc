#pragma once

// The final CS:GO build switched some UI off by adding an early return in front of code that
// still works (the player card's rank and XP with the service medal button, and its skill
// groups), added a "Legacy version of CS:GO" popup to the main menu and took Official
// Matchmaking out of the play menu. We hook the function Panorama compiles its scripts with and
// take those returns and the popup out, and put Official Matchmaking back for matches against bots.
namespace UIPatches
{

enum PatchGroup : unsigned
{
    PatchGroupDisabledUI = 1, // restore_disabled_ui
    PatchGroupMatchmaking = 2, // Official Matchmaking against bots, rank_progress
    PatchGroupAll = PatchGroupDisabledUI | PatchGroupMatchmaking,
};

// hooks v8.dll's script compiler, call before the game loads Panorama. windows only for now
void Install();

// applies the patches to a script's source, returns how many were applied. exposed for tests
int Apply(std::string &source, unsigned groups = PatchGroupAll);

} // namespace UIPatches
