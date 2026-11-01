# AI and Scripting

Three separate systems drive anything the player isn't controlling:

| System | Used in | Role |
| --- | --- | --- |
| NPC AI | SP mainly, also MP Siege and spawned NPCs | Enemies and allies, with behaviour states |
| ICARUS | SP and MP | Scripted sequences and cutscenes |
| Bot AI | MP | Stand-ins for human players |

## NPCs

### An NPC is a player with a fake controller

This is the key design decision. An NPC doesn't move itself. It's a client-like entity, and each think it writes a `usercmd_t`, then goes through the same `ClientThink` and `Pmove` as the player:

```text
NPC_Think                    ent->think, every frame or two
  NPC_ExecuteBState          decide
    NPC_RunBehavior          pick by class and weapon
      NPC_BehaviorSet_*      pick by behaviour state
  write ucmd                 forwardmove, buttons, angles
  ClientThink(ent, &ucmd)    same movement and saber code as a player
```

So NPC Jedi get real saber physics and animations without any AI-specific code path for them. They also obey the same rules: an NPC can't do a move a player couldn't.

SP's entry point is `code/game/NPC.cpp:2365`, and MP's is `codemp/game/NPC.c:1766`.

### Two-level dispatch

`NPC_RunBehavior` first picks by **type**. It's a long chain (`code/game/NPC.cpp`):

```c
if ( bState == BS_CINEMATIC )                NPC_BSCinematic();
else if ( NPC->client->ps.weapon == WP_SABER ) NPC_BehaviorSet_Jedi( bState );
else if ( NPC->client->NPC_class == CLASS_BOBAFETT ) { ... }
else if ( NPC->client->NPC_class == CLASS_RANCOR )   NPC_BehaviorSet_Rancor( bState );
```

There are `NPC_BehaviorSet_` functions for each family: Jedi, Stormtrooper, Sniper, Grenadier, Droid, Mark1, Mark2, ATST, Rancor, Wampa, Howler, SandCreature, Animal, Seeker, Sentry, Tusken and more. Each class's logic is in an `AI_*.cpp` file.

Each set then branches on the **behaviour state**, `bState_t` (`code/game/bstate.h`):

| State | Meaning |
| --- | --- |
| `BS_DEFAULT` | The class's normal AI |
| `BS_ADVANCE_FIGHT` | Move to a goal, shooting on the way |
| `BS_SLEEP` | Dormant until startled |
| `BS_FOLLOW_LEADER` | Follow and fight |
| `BS_JUMP` | Jump to a nav goal |
| `BS_SEARCH`, `BS_WANDER` | Move along waypoints |
| `BS_REMOVE` | Vanish once out of sight |
| `BS_CINEMATIC` | Under script control |
| `BS_FLEE` | Run away |
| `BS_STAND_GUARD`, `BS_PATROL`, `BS_INVESTIGATE`, `BS_HUNT_AND_KILL` | Internal states |

Scripts can set these directly, which is how designers hand an NPC over to AI at the end of a cutscene.

### Senses and memory

`NPC_senses` gives NPCs vision and hearing, including alert events. Gunfire and footsteps leave timed events that nearby NPCs can pick up. `gNPC_t` (`code/game/b_public.h:214`) holds the AI's memory, and `TIMER_` calls (`G_Timer`) stand in for most short-term state: "don't dodge again for two seconds".

`NPC_stats` loads each NPC's definition from `.npc` files, including health, model, weapons and aggression values, so most tuning is data.

Squads get group logic from `AI_Utils`, which `AI_UpdateGroups` updates each frame (`code/game/g_main.cpp`).

### The Jedi AI

`code/game/AI_Jedi.cpp` runs to nearly 7,700 lines. It's the most ambitious AI in the game, because it has to fence convincingly.

| Concern | Functions |
| --- | --- |
| Combat loop | `Jedi_Combat` (`:5798`) |
| Spacing | `Jedi_CombatDistance` (`:1351`), `Jedi_Advance`, `Jedi_Retreat` |
| Attacking | `Jedi_AttackDecide` (`:5252`) |
| Blocking | `Jedi_SaberBlockGo` (`:2948`), `Jedi_SaberBlock` (`:3839`) |
| Dodging | `Jedi_DodgeEvasion` (`:2092`), `Jedi_CheckFlipEvasions` (`:2313`) |
| Mood | `Jedi_Aggression` (`:638`), `Jedi_AggressionErosion` (`:675`) |
| Style | `Jedi_AdjustSaberAnimLevel` (`:1147`) |
| Reading the player | `Jedi_CheckEnemyMovement` (`:5419`), `Jedi_CheckEvadeSpecialAttacks` (`:3653`) |
| Jumps | `Jedi_CheckJumps` (`:5568`) |
| Other | `Jedi_BattleTaunt` (`:754`), `Jedi_Flee`, ambush logic, katas |

Aggression is the emotional core. It rises and falls with events, shapes how often a Jedi attacks, and is why a Jedi can suddenly press you or back off.

Difficulty works by scaling reaction and parry times (`Jedi_ReCalcParryTime`, `:2645`), which makes a harder enemy react faster and doesn't require different logic.

### Navigation

SP has two layers, in the `NAV` and `STEER` namespaces (`code/game/g_navigator.h`):

- **NAV** plans a route over a graph of waypoints and edges. `NAV::GoTo` follows it.
- **STEER** handles local movement: avoiding other NPCs, seeking and fleeing. `STEER::GoTo` handles short direct moves.

```cpp
STEER::Activate(NPC);
moveSuccess = STEER::GoTo(NPC, NPCInfo->goalEntity, NPCInfo->goalRadius);
if ( !moveSuccess ) moveSuccess = NAV::GoTo(NPC, NPCInfo->goalEntity);
STEER::DeActivate(NPC, &ucmd);
```

Navigation data in retail ships as `.nav` files, built from `waypoint_navgoal` and related map entities. `code/Ragl` holds graph containers.

MP has an older system, `g_nav.c` and `g_navnew.c`, with engine support in `codemp/server/NPCNav`. It can build paths on map load if none exist (`NAV_CheckCalcPaths`, `g_main.c:2862`).

## ICARUS

ICARUS is Raven's scripting language. Designers write text scripts, which are compiled to binary `.ibi` files. Retail has 1,018 of them under `scripts/`, sorted by level. `scripts/academy1/intro.ibi` is a typical example.

### What makes it different

Scripts run over game time and wait for things to finish, like a coroutine. A cutscene might read as:

```text
move NPC to mark, wait for arrival
play animation, wait
say a line, wait for the sound to finish
camera pan
```

No game state machine is needed for this. The script just blocks at each step.

### How it runs

`code/icarus` is an embeddable VM, separate from the game and connected by two interfaces (`code/icarus/IcarusInterface.h`):

- **`IIcarusInterface`:** what the game calls: `Run`, `Update`, `Completed`, `Save`, `Load`.
- **`IGameInterface`:** what ICARUS calls. The game implements `Set`, `Use`, `Kill`, `Remove`, `Play`, `Lerp2Pos`, camera functions and more.

```text
.ibi file
  CSequencer       walks the blocks, handling loops, ifs and affects
  CTaskManager     runs commands for one entity
     |   Set / Lerp2Pos / PlayIcarusSound ...
     v
  CQuake3GameInterface  (code/game/Q3_Interface.cpp)
     |                  (11,400 lines)
     v
  the game
     |
     +-- Completed(id, taskID) when it finishes
```

The important trick is **task IDs**. When a command takes time, such as walking to a point, the game stores the ID on the entity with `Q3_SetTaskID` (`code/game/Q3_Interface.cpp:506`). The script waits at that step, and when the NPC arrives, `Completed` releases it.

`CTaskManager` (`code/icarus/TaskManager.cpp`) has handlers for the script verbs: `Wait`, `Print`, `Sound`, `Rotate`, `Remove`, `Camera`, `Move`, `Kill`, `Set`, `Use`, `Signal`.

The `Set` verb is the workhorse. `setType_t` (`code/game/Q3_Interface.h`) has around 260 values covering AI state, animation, speed, weapons, parameters and much more, and each one ends up in a `Q3_Set*` function.

### Connection to the game

- `target_scriptrunner` and `spawnscript` keys start scripts from map logic.
- `behaviorSet` lets a script respond to events like pain, death or being used.
- `SVF_ICARUS_FREEZE` can hold an NPC still while a script acts.
- `G_RunFrame` calls `Update` each frame for entities with scripts.
- Saves store ICARUS state in full, so a load can resume mid-cutscene. See `docs/save games.md`.

MP has an older copy in `codemp/icarus` and `codemp/game/g_ICARUScb.c`, mostly used for Siege maps.

### Cameras

`code/cgame/cg_camera.cpp` runs the cutscene camera: pans, tracks, follows, zooms and fades, driven by the ICARUS `Camera*` calls.

## Bots

MP bots are simulated clients. Quake III's bot library is still in the engine, but Raven wrote a separate AI for Jedi combat.

`codemp/botlib` (36,000 lines) is the original id code: AAS area navigation, goals, chat and the `EA_` action layer. It's linked into the engine and reached through `trap->`.

`codemp/game/ai_main.c` is Raven's AI. `BotAIStartFrame` (`:7526`) runs each bot's `BotAI` (`:727`), and `StandardBotAI` (`:5966`) does the real work:

```text
ScanForEnemies        find a target
GetIdealDestination   choose where to go
move between waypoints
Force and saber decisions
EA_* action calls     turned into a usercmd
```

Bots navigate with **waypoints**, not AAS. `gWPArray` (`ai_main.h:411`) is loaded by `LoadPath_ThisLevel` (`codemp/game/ai_wpnav.c:3282`) from the `.wnt` files in `botroutes/`, and `bot_wp_edit` turns on in-game editing.

They have personalities under `botfiles/`, and team and Siege modes add a state machine (`teamplayState`, `ctfState`, `siegeState`).

Like NPCs, bots end up as a `usercmd_t` going through `Pmove`.
