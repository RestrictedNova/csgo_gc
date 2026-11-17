# csgo_gc

> [!CAUTION]
> This project is incomplete and not ready for general use.

In Valve games, the Game Coordinator (GC) is a backend service most notably responsible for matchmaking and inventory management (like loadouts and skins). This project redirects the GC traffic to a custom, in-process implementation, eliminating the need for a separate GC server.

## Features

- **Inventory:** Editing, equipping, case opening, trade up contracts, souvenir packages, stickers, patches, graffiti, StatTrak and StatTrak swaps, storage units, name tags, music kits and the in-game store
- **Official Matchmaking against bots** (optional): Competitive and Wingman matches on an official matchmaking server against bots of your skill group, earning your level, XP and skill groups
- **Multiplayer:** Lobbies, dedicated servers and a functional server browser
- Steam P2P networking for inventory data
- Live inventory editing via a WebSocket server
- [Multi-version support](docs/versions.md) (experimental)
- Full Windows, Linux and macOS support
- Works without full Steam API emulation

Operation-related features are not currently planned. Matchmaking against other players is not planned, as a proper implementation would require a centralized server, which this project intentionally avoids. [Official Matchmaking against bots](#official-matchmaking-against-bots) takes its place.

## Installation

- Download [CS:GO from Steam](steam://install/4465480)
- Download the latest release for your platform from the [releases page](https://github.com/RestrictedNova/csgo_gc/releases/latest)
- Navigate to the game's installation directory
- Back up your existing launcher executables as they'll be overwritten (i.e. csgo.exe, srcds.exe, csgo_linux64, etc.)
- Extract the contents of the downloaded archive to your game directory, replace the executables when prompted
- Launch the game. If you get the annoying VAC message box, launch the game with the -steam argument
- macOS users: The release binaries are not notarized, so if you're using them, you'll have to deal with that somehow

## Inventory editing

### Realtime editing

The GC exposes a local WebSocket API for inventory editors. An editor can read the inventory and send changes while the game is running. Changes appear in-game immediately and are saved to inventory.txt. In-game inventory changes are also sent to the connected editor.

A web-based realtime editor is available [here](https://github.com/RestrictedNova/csgo_invedit). The default `inventory_editor_origin` in `csgo_gc/config.txt` allows connections from its site. To use another editor, set it to the `Origin` that editor provides.

### Offline editing

See [available GUI editors](https://github.com/mikkokko/csgo_gc/issues/82). There is also a guide for editing inventory.txt by hand [here](https://gist.github.com/dricotec/1ae3deb06c42012970c00df914348e76) if you want to do that for some reason.

## Official Matchmaking against bots

Set `"rank_progress" "1"` in `csgo_gc/config.txt` to earn your level, XP and skill groups by playing against bots (Windows only for now). Your progress is saved to `csgo_gc/progress.txt`. A new progress file starts like a new account: level 1 and no skill groups. This replaces the fixed `ranks`, `player_level` and `player_cur_xp` options.

It brings Official Matchmaking back to the play menu for Competitive and Wingman:

1. Pick your maps and press Go. A match is found after a few seconds and the accept popup appears.
2. Accept it before the timer runs out. If you don't, you're removed from the queue, like in CS:GO.
3. The match runs on your own machine as an official matchmaking server: warmup, no team select, official rules, and bots in the other places. Competitive uses your long or short match setting.
4. When the match ends, the RANK and SKILL GROUP screens show the XP you earned, level ups and skill group changes.

### Progress

- Only matches found by Official Matchmaking count. Practice matches against bots don't.
- Skill groups work like CS:GO's: a hidden Glicko-2 rating that shows after 10 wins in that mode and expires after 30 days without a win.
- Leaving a match before it ends counts as a loss and earns no XP.
- XP is 30 per round won in Competitive and 15 in Wingman, with the weekly XP bonus. Every 5000 XP is a level. At level 40 you can claim a service medal.

### Bots

The bots play like players of your skill group. There is one tier of bots for each of the 18 skill groups, chosen from your rating when a match is found. They get better as you rank up, but don't change during a match.

The tiers are based on [csst.at](https://csst.at/)'s averages of Leetify stats by FACEIT level, specifically time to damage: how long players take to hurt an enemy after spotting them. It ranges from 680 ms at Silver I to 515 ms at The Global Elite. The bots' reaction time, aim, skill and aggression scale with it, from about Valve's Normal bots at Silver I to its hardest Expert bots at The Global Elite. In your rating, the bots count as players of their skill group.

## Configuration

See [csgo_gc/config.txt](examples/config.txt) for available options.

## Bug reports and support

Report bugs by opening an issue and following the instructions. If the issue doesn't appear to be strictly GC related, verify that it doesn't happen on a clean game (no csgo_gc installed).

For support, ideas, and general discussion, use [Discussions](https://github.com/mikkokko/csgo_gc/discussions).

## Building

Requires Git, vcpkg, CMake 3.20+, and a compiler with C++20 support. See [the continuous build workflow](.github/workflows/build.yml) for details.

## License

[2-Clause BSD](LICENSE)

## Credits
* **Mikko Kokko** - Author
* **Theeto** - Code reused from the predecessor project, unusual loot lists
