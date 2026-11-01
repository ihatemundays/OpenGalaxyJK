# Jedi Academy: A Developer's Guide to the Game

This guide explains how *Star Wars Jedi Knight: Jedi Academy* works, using the OpenJK source in this repository and the retail assets it runs on. It is written for someone who wants to change the game, not just build it.

Paths are relative to the repo root. `file:line` references were correct at commit `260c59c2`, so treat line numbers as a starting point.

## What the game is

Jedi Academy (Raven Software, 2003) is a third-person action game built around lightsaber combat and Force powers. It runs on a heavily extended Quake III Arena engine, which Raven had already reworked for Jedi Outcast.

It is really two games sharing an engine family:

- **Single player (SP):** a campaign as the student Jaden Korr. Levels are scripted, NPC-heavy and saved to disk.
- **Multiplayer (MP):** client/server arena play with bots, from free-for-all and duels through to objective-based Siege.

Raven's main additions to Quake III are what give the game its character:

| Addition | What it does | Read more |
| --- | --- | --- |
| Saber combat | A move table of swings, parries, locks and katas, resolved by tracing the blade through the world | [Combat](05-combat.md) |
| Force powers | 18 upgradeable abilities running on a shared energy pool | [Combat](05-combat.md) |
| Ghoul2 | Skeletal animation, per-limb hit detection, dismemberment and ragdoll | [Rendering](08-rendering-and-models.md) |
| ICARUS | A scripting language that drives cutscenes and level events | [AI and scripting](06-ai-and-scripting.md) |
| NPC AI | Behaviour states for over 20 enemy families, including Jedi who fence | [AI and scripting](06-ai-and-scripting.md) |
| Vehicles | Walkers, speeders, fighters and rideable animals | [Vehicles and modes](07-vehicles-and-game-modes.md) |
| Effects system | Data-driven particle effects in `.efx` files | [Rendering](08-rendering-and-models.md) |

## The one idea to hold on to

Every Quake III descendant is built around this split:

> The **server game** owns the truth. The **client game** draws a smoothed, predicted version of it. **Shared `bg_` code** is compiled into both, so they agree on how a player moves.

Even single player follows this. It runs a local server and client that talk over an in-memory loopback, which is why SP code still has snapshots and prediction.

## Reading order

1. [Architecture](01-architecture.md): programs, modules, startup, the main loop, memory, files
2. [Simulation and networking](02-simulation-and-networking.md): how one keypress becomes a world change on every screen
3. [World and entities](03-world-and-entities.md): maps, collision and the entity system
4. [Movement and animation](04-movement-and-animation.md): `Pmove` and the two-part body
5. [Combat](05-combat.md): sabers, Force powers, weapons, damage
6. [AI and scripting](06-ai-and-scripting.md): NPCs, navigation, ICARUS, bots
7. [Vehicles and game modes](07-vehicles-and-game-modes.md): vehicles, the campaign, MP modes
8. [Rendering and models](08-rendering-and-models.md): renderers, shaders, Ghoul2, effects
9. [Audio, UI and content](09-audio-ui-and-content.md): sound, menus and the retail data files

Related notes already in `docs/`: [library loading](../libraries.md), [save games](../save%20games.md), [language strings](../language%20strings.md), [renderer overview](../renderer-architecture.md), [sound code plans](../sound-code-declutter.md).

## Repository map

| Path | Lines (approx.) | Contents |
| --- | --- | --- |
| `code/` | 420k | Jedi Academy single player: engine, game, cgame, UI, renderer |
| `codemp/` | 600k | Multiplayer: engine, server game, cgame, UI, botlib, three renderers |
| `codeJK2/` | 185k | Jedi Outcast single player game code, off by default in CMake |
| `shared/` | 11k | OS, SDL and math code used by every program |
| `lib/` | 150k | Bundled SDL2, libjpeg, libpng, zlib, minizip, gsl-lite |
| `cmake/`, `CreateVisualStudio*.bat` | | Build system |

SP and MP were forked long ago. Files with the same name, such as `bg_pmove`, have drifted apart. **A fix in one is not a fix in the other**, so check both. SP's `code/game/bg_pmove.cpp` is about 15,100 lines against 11,200 in `codemp/game/bg_pmove.c`.

## Running it from a source build

A build produces several programs and libraries, and the game needs the retail `.pk3` files. These are the failures you are most likely to hit on Windows:

| Symptom | Cause | Fix |
| --- | --- | --- |
| Exit `0xC0000135` | `SDL2.dll` or `libwinpthread-1.dll` not found | Put both next to the exe. `SDL2.dll` is in `lib/SDL2/bin/x64`, the other in the MinGW `bin` folder |
| `Couldn't load default.cfg` or `mpdefault.cfg`, `0 files in pk3 files` | No game data on the search path | Launch with `+set fs_basepath "<install>\GameData"` |
| Crash on the first menu frame, with a bad jump inside `uix86_64.dll` | Newer engine loaded an old module | Rebuild the modules and make sure the new ones are found first |

Building a single exe target does not rebuild its modules. Module search starts in the home path, then the base path, so a stale copy in either can silently win. On Windows the home path is `Documents\My Games\OpenJK`, which may be under OneDrive.

The startup logo is `cinematic openinglogos`, played when `com_bootlogo` is non-zero and no startup command was given (`code/qcommon/common.cpp:1190`). Passing `+map` skips it, but `+set` does not.

## Current state of this tree

`code/ui_rml/` and `tools/menu_converter/` exist as empty directories, and nothing in the build references RmlUi. The UI described here is the legacy `.menu` system.
