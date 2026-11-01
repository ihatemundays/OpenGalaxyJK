# Movement and Animation

## Pmove: the player physics

`Pmove` (`codemp/game/bg_pmove.c:11172`) takes a `playerState_t` and a `usercmd_t` and produces the next `playerState_t`. Everything that moves a character goes through it: humans, NPCs and vehicles. It's compiled into both game and cgame, which is what makes prediction possible.

Its signature is effectively a pure function of its inputs, plus world traces:

```c
pmove_t pm;
pm.ps = &client->ps;
pm.cmd = *ucmd;
pm.trace = trap->Trace;
Pmove( &pm );
```

### Staying frame-rate independent

`Pmove` slices long commands so a slow frame doesn't change the physics:

```c
msec = finalTime - pmove->ps->commandTime;
if ( msec > 66 ) msec = 66;
pmove->cmd.serverTime = pmove->ps->commandTime + msec;
PmoveSingle( pmove );
```

`pmove_fixed` forces a constant step on servers that want identical physics for everyone.

### One step

`PmoveSingle` (`codemp/game/bg_pmove.c:10167`) does far more than move. It's also where combat moves are chosen. In order:

```text
Restrict input (katas, meditate, knockdown, freezes)
PM_CmdForSaberMoves         a saber move may steer the player
BG_AdjustClientSpeed        speed from stance, Force, saber state
PM_AdjustAttackStates
PM_UpdateViewAngles
PM_SetWaterLevel
PM_GroundTrace              what am I standing on?
PM_DropTimers
pick a mover:
  PM_FlyVehicleMove / PM_WaterMove / PM_WalkMove / PM_AirMove
PM_Animate
PM_GroundTrace again
PM_Weapon                   includes the saber state machine
PM_Use
PM_Footsteps                picks run and walk animations
PM_WaterEvents
```

`STEPSIZE` is 18 and `DEFAULT_GRAVITY` is 800 (`codemp/game/bg_public.h`).

`bg_slidemove` does the collision response. `PM_SlideMove` traces along the velocity and clips against whatever it hits, up to a few bounces. `PM_StepSlideMove` lets the player walk up small ledges.

### Jedi movement

This is where the game parts ways with Quake III:

- **Force jump:** holding jump charges height through `FP_LEVITATION` (`ForceJumpCharge`, `codemp/game/w_force.c:2171`). Level sets the height, and `JUMP_VELOCITY` is 225.
- **Wall runs, flips and wall jumps:** `PM_AdjustAngleForWallRun`, `PM_AdjustAngleForWallJump` and similar take over view angles during a move.
- **Rolls, cartwheels and backflips:** these are animation-led, with `BG_CmdForRoll` overriding input.
- **Knockdowns:** the player loses control until the get-up animation finishes.

## Time as state

`playerState_t` is full of "time remaining" fields: `weaponTime`, the leg and torso animation timers (`legsTimer` and `torsoTimer` in MP, `legsAnimTimer` and `torsoAnimTimer` in SP) and many others. `PM_DropTimers` counts them down. Because they live in the shared state, client and server agree on them.

A common pattern for gameplay code is setting one of these and letting shared code enforce the lockout, so don't add a separate server-only timer for movement. It won't be predicted.

## Animation is split in two

Humanoids have independent **legs** and **torso** animations:

- `ps->legsAnim` plays run, jump and idle.
- `ps->torsoAnim` plays attacks and gestures.
- `BOTH_` animations drive both, for flips and full-body moves.

This is why a player can run while swinging. The animation list is an enum in `anims.h` with a matching name table in `animtable.h`.

`PM_SetAnim` and `PM_ContinueLegsAnim` (`bg_panimate`) request a new animation. Priority flags decide if it can interrupt the current one:

| Flag | Meaning |
| --- | --- |
| `SETANIM_FLAG_OVERRIDE` | Replace the current animation |
| `SETANIM_FLAG_HOLD` | Block others until the hold ends |
| `SETANIM_FLAG_RESTART` | Restart even if it's already playing |
| `SETANIM_FLAG_HOLDLESS` | Hold, but with a shorter blocked time |

Because the animation choice is in `Pmove`, the client predicts it and the server only confirms.

### Animations are gameplay data

In most engines animation follows gameplay. In this game they are locked together:

- Move duration comes from animation length (`PM_AnimLength`), so a swing lasts as long as its animation.
- Saber damage comes from the real blade position in the animation.
- `animation.cfg`, stored with each skeleton, gives frame ranges and speeds.
- `animevents.cfg` fires sounds and effects on specific frames (`codemp/game/bg_panimate.c:2126`).

Editing an animation therefore rebalances combat. Editing `animation.cfg` changes server gameplay, not just visuals.

### Weapons and items

`PM_Weapon` runs the fire state machine: ready, firing, dropping, raising, tracked through `weaponstate` and `weaponTime`. For guns it just generates fire events. For the saber it hands over to `PM_WeaponLightsaber` (`codemp/game/bg_saber.c:2777`), which is covered in [Combat](05-combat.md).

### Ragdoll

When a character dies, `EF_RAG` can hand the skeleton to the Ghoul2 ragdoll solver, which is covered in [Rendering and models](08-rendering-and-models.md). This is purely visual and runs in the cgame.
