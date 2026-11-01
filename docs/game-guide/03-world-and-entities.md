# World and Entities

## The map file

A level is a `.bsp` in `maps/`, with the magic `RBSP` and version 1 (`codemp/qcommon/qfiles.h:307`). It's a compiled world split into 18 "lumps":

| Group | Lumps | Used by |
| --- | --- | --- |
| Rendering | `DRAWVERTS`, `DRAWINDEXES`, `SURFACES`, `SHADERS`, `LIGHTMAPS`, `FOGS` | Renderer |
| Collision | `PLANES`, `BRUSHES`, `BRUSHSIDES`, `LEAFBRUSHES` | Collision model |
| Space | `NODES`, `LEAFS`, `LEAFSURFACES` | Both |
| Lighting | `LIGHTGRID`, `LIGHTARRAY` | Lighting moving models |
| Visibility | `VISIBILITY` | Culling and snapshots |
| Logic | `ENTITIES`, `MODELS` | Game code |

Compared to Quake III, Raven supports up to four lightmaps per surface (`MAXLIGHTMAPS`), so lights can switch and pulse through light styles.

### How the space is organized

A BSP tree recursively splits the map by planes down to leaves. Leaves group into:

- **Clusters** for visibility. `VISIBILITY` stores which clusters can see which, called the PVS. It is precomputed, so runtime checks are cheap.
- **Areas**, which are rooms separated by doors. An area portal closes when its door shuts, so `CM_AdjustAreaPortalState` can hide whatever is behind a closed door.

The same data serves rendering and networking: what you can't see isn't drawn and usually isn't sent.

## Collision

The collision model (`cm_*.cpp` in each `qcommon`) answers two questions.

```c
CM_BoxTrace( &trace, start, end, mins, maxs, model, contentmask, capsule );
CM_PointContents( point, model );
```

`CM_BoxTrace` sweeps a box through the world and reports how far it got, the plane it hit and the surface flags. This one operation carries much of the game: movement, weapon fire, line of sight and saber contact all use traces.

Content masks decide what a trace can hit, such as `CONTENTS_SOLID`, water, or `CONTENTS_LIGHTSABER`.

Curved surfaces collide through `cm_patch`, which turns them into small facets.

Against entities, the server layer `sv_world.cpp` keeps a spatial tree of linked entities. Game code calls `trap->Trace`, which combines the world and entity tests. An entity is only collidable after `trap->LinkEntity`.

Hit detection on players is more detailed. A trace can use Ghoul2 collision to test the skeletal mesh itself, which is how the game knows you struck an arm and not just a bounding box.

## The entity system

Almost everything that isn't static world geometry is an entity: players, NPCs, doors, triggers, rockets, dropped items, the thrown saber.

`gentity_t` (`codemp/game/g_local.h:174`) is one large struct in a fixed array, `g_entities`. There is no class hierarchy, so a door and a rocket are the same type with different fields set. Its layout matters:

```c
struct gentity_s {
    entityState_t  s;           // networked to clients
    playerState_t *playerState; // for players and NPCs
    Vehicle_t     *m_pVehicle;
    void          *ghoul2;
    ...                         // up to here matches bgEntity_t and centity_t
    entityShared_t r;           // shared with the server engine
    ...                         // ICARUS fields, then game-only fields
};
```

The top of the struct must stay in sync with `bgEntity_t` and `centity_t`, because shared code casts between them. Reordering fields there breaks things silently.

| Struct | Lives in | Purpose |
| --- | --- | --- |
| `gentity_t` | Server game | The full object |
| `gclient_t` | Server game | Extra data for players and NPCs |
| `gNPC_t` | Server game | AI memory (`code/game/b_public.h:214`) |
| `entityState_t` | Network | What clients are told |
| `centity_t` | cgame | Client copy, with interpolation |
| `refEntity_t` | Renderer | One model to draw this frame |

Index ranges are meaningful. Entities below `MAX_CLIENTS` (32) are always players. The cap is about 1024 entities, and `ENTITYNUM_WORLD` and `ENTITYNUM_NONE` are reserved at the top.

### Behaviour through callbacks

An entity gets its behaviour from function pointers:

| Callback | Fires when |
| --- | --- |
| `think` | `level.time` reaches `nextthink` |
| `touch` | Something collides with it |
| `use` | A player or target activates it |
| `pain` | It takes damage and survives |
| `die` | Its health runs out |
| `reached` | A mover arrives |
| `blocked` | Something stops a mover |

SP stores these as enums, as covered in [Simulation and networking](02-simulation-and-networking.md).

### Spawning

The `ENTITIES` lump is plain text: lists of key/value pairs written by the level designer.

```text
{
"classname" "func_door"
"targetname" "gate1"
"speed" "100"
}
```

`G_SpawnEntitiesFromString` (`codemp/game/g_spawn.c:1574`) reads each block, and `G_SpawnGEntityFromSpawnVars` (`:889`) copies known keys into fields. `G_CallSpawn` (`:700`) then looks up the `classname` and calls the matching `SP_` function. SP has 207 entries in that table.

The prefix tells you the family:

| Prefix | Examples | Purpose |
| --- | --- | --- |
| `info_` | `info_player_start` | Spawn points and markers |
| `func_` | `func_door`, `func_breakable` | Moving or interactive brushes |
| `trigger_` | `trigger_multiple`, `trigger_hurt` | Invisible volumes |
| `target_` | `target_relay`, `target_scriptrunner` | Logic that fires when triggered |
| `misc_` | `misc_model`, `misc_turret` | Props and devices |
| `NPC_` | `NPC_Tavion` | Character spawners |
| `weapon_`, `ammo_`, `item_` | `weapon_rocket_launcher` | Pickups |

### Wiring a level

Designers connect entities with text names. A button's `target` names another entity's `targetname`, and `G_UseTargets` fires them. A whole puzzle can be built this way with no code: trigger, relay, delay, door. ICARUS scripts take over when logic needs to run in sequence.

## Movers and ROFF

`g_mover` handles doors, platforms and rotating brushes. They move on trajectories, so clients can compute their positions without a network update each frame, and they drag along whatever stands on them.

ROFF files (`.rof`, with 92 in retail) store a recorded motion path. They can be played on entities for complex animated set pieces (`codemp/qcommon/RoffSystem.cpp`).

## Trajectories

Entities in flight don't send position every frame. `entityState_t` holds a `trajectory_t`: a type, start time, base position and velocity. Both sides can evaluate that for any time, so a rocket stays smooth at a low snapshot rate.

## Events

Sounds and effects tied to a moment travel as events. Game code calls `G_AddEvent`, or `PM_AddEvent` in shared code. The event goes out in entity state, and `CG_EntityEvent` plays it.

Events in `Pmove` are predicted, so the local player hears their own footsteps at once. The server marks them so they aren't played a second time.
