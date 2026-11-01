# Vehicles and Game Modes

## Vehicles

Jedi Academy added ridable vehicles on top of the character system. There are four kinds (`codemp/game/bg_vehicles.h:32`):

| Type | Example | Code |
| --- | --- | --- |
| `VH_WALKER` | AT-ST | `WalkerNPC.c` |
| `VH_FIGHTER` | X-Wing, TIE fighter | `FighterNPC.c` |
| `VH_SPEEDER` | Swoop bike | `SpeederNPC.c` |
| `VH_ANIMAL` | Tauntaun | `AnimalNPC.c` |

### A vehicle is an NPC

A vehicle is an NPC entity of class `CLASS_VEHICLE`, so it uses a full client slot and goes through `ClientThink` and `Pmove`. When you ride one, your input is copied across (`codemp/game/g_active.c:1867` onward):

```c
memcpy( &veh->m_pVehicle->m_ucmd, &ent->client->pers.cmd, sizeof(usercmd_t) );
```

The pilot's usercmd then drives the vehicle's own `Pmove`. `PM_FlyVehicleMove` runs flight and `BG_FighterUpdate` handles fighter physics. MP prediction has to run both the pilot and the vehicle, which is why `cg.predictedVehicleState` exists.

### Virtual methods in plain C

`vehicleInfo_t` (`bg_vehicles.h:142`) holds both data and function pointers, acting like a C++ vtable:

```c
qboolean (*Board)( Vehicle_t *pVeh, bgEntity_t *pEnt );
qboolean (*Eject)( Vehicle_t *pVeh, bgEntity_t *pEnt, qboolean forceEject );
void (*ProcessMoveCommands)( Vehicle_t *pVeh );
void (*ProcessOrientCommands)( Vehicle_t *pVeh );
void (*AnimateVehicle)( Vehicle_t *pVeh );
```

Each kind sets its own in `G_SetSpeederVehicleFunctions`, `G_SetFighterVehicleFunctions` and so on. `Vehicle_t` (`:484`) is the per-instance state: pilot, passengers, armor, orientation.

The game handles boarding, ejecting, passenger seats, turrets and part damage (`m_iRemovedSurfaces`). Fighters even check their hyperspace state in `Pmove`.

### Vehicle data

Vehicles are defined in data:

- `.veh` files in `ext_data/vehicles` (18 in retail): handling, speed, health, seats, model
- `.vwp` files in `ext_data/vehicles/weapons` (14 in retail): vehicle weapons

They're loaded by `bg_vehicleLoad.c`.

## The single player campaign

SP's structure comes from its map names:

| Maps | Role |
| --- | --- |
| `academy1`-`6` | Hub levels at the Jedi Academy, between missions |
| `kor1`, `kor2` | Korriban, near the end of the game |
| `t1_*` | First tier of missions: `danger`, `fatal`, `inter`, `rail`, `sour`, `surprise` |
| `t2_*` | Second tier: `dpred`, `rancor`, `rogue`, `trip`, `wedge` |
| `t3_*` | Third tier: `bounty`, `byss`, `hevil`, `rift`, `stamp` |
| `hoth2`, `hoth3`, `vjun1`-`3`, `taspir1`-`2`, `yavin1`, `yavin1b`, `yavin2` | Fixed story missions |

The campaign alternates scripted story missions with tiers where the player picks from a set in any order, returning to an Academy hub between them. Character progression is earned here, with points spent on Force powers between missions. The story's moral choice comes near the end, and the alignment of the powers you've invested in follows the same light and dark split as MP.

### Supporting systems

- **Objectives:** `code/game/objectives.h` and `g_objectives.cpp` track mission goals, set by scripts.
- **Saves:** `code/server/sv_savegame.cpp` plus `ojk_saved_game*`, covering entities, ICARUS, effects and Ghoul2. See `docs/save games.md`.
- **Mission persistence:** `g_session` and cvars carry player state across map changes.
- **Character creation:** the SP UI builds the player's look from `g_char_model` and related cvars.
- **Cinematics:** ROQ videos, 14 in retail, are played by `cl_cin.cpp`. Most story scenes are in-engine and run by ICARUS.
- **Credits:** `code/cgame/cg_credits.cpp`.

## Multiplayer modes

Game types are in `gametype_t` (`codemp/game/bg_public.h:235`):

| Mode | Summary |
| --- | --- |
| `GT_FFA` | Free for all |
| `GT_HOLOCRON` | FFA with Force-granting holocrons |
| `GT_JEDIMASTER` | Everyone hunts the player with the Jedi Master saber |
| `GT_DUEL` | One-on-one with a queue |
| `GT_POWERDUEL` | One strong player against two weaker ones |
| `GT_SINGLE_PLAYER` | Single player FFA |
| `GT_TEAM` | Team deathmatch |
| `GT_SIEGE` | Asymmetric objective play |
| `GT_CTF` | Capture the flag |
| `GT_CTY` | Capture the ysalamiri |

The mode lives in `level.gametype`. The rules live in code branches throughout `g_main.c`, `g_client.c`, `g_team.c` and `g_combat.c`, so there's no single "mode class". `CheckExitRules` (`g_main.c:1915`) decides when a round ends.

Available maps per mode are listed in `scripts/*.arena` files.

### Force and server settings

Servers control the balance with cvars such as `g_maxForceRank`, `g_forcePowerDisable` and `g_weaponDisable`. A client's Force setup is checked against them in `WP_InitForcePowers`.

### Siege

Siege is the biggest MP mode: one team attacks through a chain of objectives and the other defends, then roles swap and the time is compared.

- `codemp/game/g_saga.c` runs it: `InitSiegeMode` (`:116`), `SiegeBeginRound` (`:874`), `SiegeRoundComplete` (`:679`).
- `bg_saga.c` loads the data: classes from `ext_data/Siege/Classes/*.scl` (57 in retail) and teams from `ext_data/Siege/Teams/*.team` (10 in retail).
- Classes set the loadout: weapons, Force powers, health, speed and model.
- Respawning comes in waves through `g_siegeRespawn` in `G_RunFrame`.
- Objectives and mobile items use `siegeTriggerUse` and `SiegeItemThink`.
- Maps often use ICARUS, vehicles and NPCs, which is why those systems exist in MP.

### Duels

In a private duel, `ps.duelInProgress` marks the two opponents. A duel can end with the slow-motion `gDoSlowMoDuel` effect.
