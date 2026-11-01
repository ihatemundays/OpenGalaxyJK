# Simulation and Networking

This is the path one keypress takes to become a change on every player's screen.

```text
CLIENT                                       SERVER
keys + mouse
   | CL_CreateCmd
   v
usercmd_t ------- UDP or loopback ---------> SV_ClientThink
   |                                            | ClientThink_real
   | CG_PredictPlayerState                      | Pmove (authoritative)
   | Pmove (a local guess)                      | G_RunFrame
   v                                            v
draw now                                     SV_BuildClientSnapshot
   ^                                            |
   | CG_ProcessSnapshots                        |
   +------------ delta snapshot <---------------+
```

## 1. Input becomes a usercmd

A `usercmd_t` is one tick of intent, and it holds no positions (`codemp/qcommon/q_shared.h:1424`):

```c
typedef struct usercmd_s {
    int         serverTime;
    int         angles[3];
    int         buttons;
    byte        weapon, forcesel, invensel, generic_cmd;
    signed char forwardmove, rightmove, upmove;
} usercmd_t;
```

`cl_input.cpp` builds these. `CL_KeyMove` and `CL_MouseMove` fill in movement and view, and `CL_CmdButtons` fills in buttons (`codemp/client/cl_input.cpp:879`). `CL_WritePacket` sends the latest commands, and includes recent earlier ones so a lost packet costs little.

A client can only send intent. It can't claim a position or a hit, which is what makes the server authoritative.

## 2. The server applies it

`SV_ExecuteClientMessage` reads the packet and passes each command to `SV_ClientThink`, which calls into the game. `ClientThink_real` (`codemp/game/g_active.c:1867`) then:

1. Handles special states such as vehicles, freezes and scripted locks
2. Builds a `pmove_t` and runs `Pmove`
3. Copies the result into the networked entity with `BG_PlayerStateToEntityState`
4. Runs `ClientEvents`, `G_TouchTriggers` and `ClientImpacts`

Moves happen when commands arrive, not on the server tick. Everything else, including saber damage and Force powers, happens in `G_RunFrame`.

## 3. The server frame

`G_RunFrame` (`codemp/game/g_main.c:2919`) walks every entity and dispatches by type:

```text
for each entity:
  missile  -> G_RunMissile
  item     -> G_RunItem
  mover    -> G_RunMover
  client   -> WP_ForcePowersUpdate
              WP_SaberPositionUpdate
              WP_SaberStartMissileBlockCheck
  others   -> G_RunThink
  ICARUS task maintenance
then ROFF updates, ClientEndFrame for each player,
CheckTournament, CheckExitRules, CheckTeamStatus, CheckVote
```

## 4. Snapshots

The server doesn't send "what changed in the world". It works out what each client can see and sends that as a snapshot.

- `SV_BuildClientSnapshot` (`codemp/server/sv_snapshot.cpp:543`) uses the map's visibility data to pick entities.
- A snapshot holds the client's own `playerState_t` plus an `entityState_t` for each visible entity.
- **Delta compression:** each snapshot is encoded against one the client has acknowledged. Field tables list what is sent and how many bits it takes (`entityStateFields`, `codemp/qcommon/msg.cpp:832`; `playerStateFields`, `:1303`).
- **Huffman coding** then compresses the bytes, using a static table (`msg.cpp:32`).

The two state structs are very different:

| | `playerState_t` | `entityState_t` |
| --- | --- | --- |
| Sent to | That player only | Everyone who can see it |
| Holds | Full detail: health, Force, timers | The minimum needed to draw it |
| Used for | Prediction and HUD | Rendering |

Adding a field to either costs bandwidth and needs a matching table entry. Changing the wire format breaks compatibility with retail clients.

### Everything else that crosses the wire

| Mechanism | Used for |
| --- | --- |
| Configstrings | Reliable shared strings: model names, sound names, scores, map info. Entities send a small index instead of a name |
| Server commands | Reliable text commands, such as chat and center prints |
| Events | One-shot effects on an entity, like footsteps or impacts |
| Userinfo | The client's name, model and settings |

## 5. The transport layer

`net_chan.cpp` adds sequencing and fragmentation on top of UDP. `MAX_PACKETLEN` is 1400 bytes, and larger messages are split. A "qport" lets a client survive a router changing its source port. Out-of-band packets, with sequence -1, handle things like connection and server queries.

## 6. Connecting

`SV_DirectConnect` (`codemp/server/sv_client.cpp:131`) validates the client. `SV_SendClientGameState` then sends all configstrings and baselines, which are the default entity states that deltas start from. The client loads the map and media, and `SV_ClientEnterWorld` puts it in the game.

## 7. Prediction on the client

Round-trip latency would make movement feel terrible if the client waited for the server, so it doesn't wait. `CG_PredictPlayerState` (`codemp/cgame/cg_predict.c`) does this every rendered frame:

1. Copy the latest `playerState_t` from the server
2. Re-run `Pmove` on every command the server hasn't acknowledged yet
3. Draw the player at the result

This only works because `Pmove` is the same `bg_` code on both sides and is deterministic. Each new snapshot is effectively a correction. If the guess was wrong, the error is blended out so the view doesn't snap.

This is the most important rule for gameplay code:

> Anything that affects movement must live in `bg_` code and depend only on data available to both sides. Otherwise the client mispredicts and the player sees jitter.

## 8. Interpolation

Other players can't be predicted because their input is unknown. Instead, the client shows the world slightly in the past, between two snapshots:

- `CL_SetCGameTime` (`codemp/client/cl_cgame.cpp:712`) picks a display time behind the latest snapshot.
- `CG_ProcessSnapshots` moves `cg.snap` and `cg.nextSnap` along (`codemp/cgame/cg_snapshot.c:366`).
- `CG_TransitionSnapshot` fires new events as snapshots become current.

So you see yourself in the present and everyone else slightly in the past. That gap is why hits can feel off at high ping.

## 9. Drawing a frame

`CG_DrawActiveFrame` (`codemp/cgame/cg_view.c`) runs once per rendered frame:

```text
update cvars, clear the scene
CG_ProcessSnapshots
send weapon and Force selection to the engine (SetUserCmdValue)
CG_PredictPlayerState
decide first or third person
add players, entities, effects, marks to the scene
RenderScene, then the 2D HUD
```

## 10. Single player differences

SP uses the same structure over `NET_SendLoopPacket` (`code/qcommon/net_chan.cpp:486`), which is an in-memory queue, with no sockets.

| | Single player | Multiplayer |
| --- | --- | --- |
| Game and cgame | One DLL | Two DLLs |
| Saber and Force update | In `ClientThink_real` (`code/game/g_active.cpp:5238`) | In `G_RunFrame` |
| Game object methods | `e_ThinkFunc` enums through `g_functions.cpp` | Plain function pointers |
| Save and load | Full state serialization | None |

The SP function-enum design exists for saves. A raw function pointer can't be written to disk and restored reliably, but an enum can.
