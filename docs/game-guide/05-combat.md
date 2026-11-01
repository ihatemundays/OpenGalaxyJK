# Combat

Combat is the reason this game exists. It's split across a few big files:

| System | MP | SP |
| --- | --- | --- |
| Choosing saber moves (shared) | `codemp/game/bg_saber.c` | `code/game/bg_panimate.cpp`, `bg_pmove.cpp` |
| Saber hits and blocks | `codemp/game/w_saber.c` | `code/game/wp_saber.cpp` |
| Force powers | `codemp/game/w_force.c` | `code/game/wp_saber.cpp` |
| Guns | `codemp/game/g_weapon.c`, `bg_weapons.c` | `code/game/wp_*.cpp`, `g_weapon.cpp` |
| Damage | `codemp/game/g_combat.c` | `code/game/g_combat.cpp` |

SP and MP combat look alike but are separate implementations with different tuning. Unless noted, examples below are MP.

## The lightsaber

### Three layers

```text
Input + stance  -> PM_WeaponLightsaber picks a move (shared, predicted)
Move            -> an animation swings the blade
Blade position  -> CheckSaberDamage traces it and resolves hits (server)
```

There's no "attack hit" flag and no hitbox on the swing. A blade is a line segment attached to the hand, and the game sweeps it through space.

### Styles

A saber's stance is `saberAnimLevel` (`codemp/game/bg_public.h:1549`):

| Value | Stance | Character |
| --- | --- | --- |
| `SS_FAST` | Blue | Quick, weak, chains freely |
| `SS_MEDIUM` | Yellow | Balanced |
| `SS_STRONG` | Red | Slow, heavy, breaks parries |
| `SS_DESANN` | Desann | Boss style |
| `SS_TAVION` | Tavion | Boss style |
| `SS_DUAL` | Dual sabers | Two blades |
| `SS_STAFF` | Saber staff | Two-ended blade |

### The move table

`saberMoveData[]` (`codemp/game/bg_saber.c:148`) is the core data of saber combat. Each row is one move:

```c
//                 animation       start  end   flags        blend  block      chain idle   chain attack  length
{"TL2BR Att", BOTH_A1_TL_BR, Q_TL, Q_BR, AFLAG_ACTIVE, 100, BLK_TIGHT, LS_R_TL2BR, LS_R_TL2BR, 200},
```

A swing goes from one point on the clock face to another. The eight quadrants are named `Q_TL`, `Q_T`, `Q_TR`, `Q_R`, and so on.

Moves have a lifecycle:

```text
LS_READY -> LS_S_*  start: raise the blade to the swing's start
         -> LS_A_*  attack: the dangerous part
         -> LS_R_*  return to ready
         -> LS_T1_* transition: go directly into the next swing
```

- **Direction comes from your movement keys.** `PM_SaberMoveQuadrantForMovement` (`bg_saber.c:653`) maps keys to a quadrant. Strafe left and attack gives a horizontal swing, and so on.
- **Chaining:** `PM_SaberAnimTransitionAnim` (`:427`) connects the end of one swing to the start of the next via `transitionMove`.
- **Chain limits:** `PM_SaberKataDone` (`:747`) stops a chain after a style-dependent number of swings.

### Special moves

`PM_SaberAttackForMovement` (`bg_saber.c:2182`) looks for situations before choosing a normal swing:

| Function | Move |
| --- | --- |
| `PM_SaberLungeAttackMove` (`:1776`) | Crouch lunge |
| `PM_SaberJumpAttackMove` (`:1888`) | Jump attack |
| `PM_SaberFlipOverAttackMove` (`:1597`) | Flip over an enemy |
| `PM_SaberBackflipAttackMove` (`:1696`) | Backflip attack |
| `PM_CheckStabDown` (`:587`) | Stab a downed enemy |
| `PM_CheckPullAttack` (`:2058`) | Pull an enemy onto the blade |
| `PM_CanDoKata` (`:2650`) | Kata |
| `PM_CheckAltKickAttack` (`:2691`) | Kick |

Many need enough Force power, checked by `BG_EnoughForcePowerForMove` (`:130`).

### Resolving a hit

Every server frame, `WP_SaberPositionUpdate` (`codemp/game/w_saber.c:8091`) gets each blade's position from the Ghoul2 skeleton. A blade moves a long way in one frame at sword-swing speed, so `G_SPSaberDamageTraceLerped` (`:5252`) interpolates between the previous and current positions, and `CheckSaberDamage` (`:3835`) traces those.

`CheckSaberDamage` is about 1,400 lines, and it is the referee of the duel. It decides:

1. **Damage:** base `SABER_HITDAMAGE` is 35. Dual and staff attacks ramp through `G_GetAttackDamage` up to 70, specials reach 90, and the dual kata does 90.
2. **Did blades meet?** `WP_SabersIntersect` (`:2830`) tests the two segments.
3. **Who wins a clash?** It compares attack strength, stance and `FP_SABER_OFFENSE`/`DEFENSE` levels, including `G_SaberAttackPower` (`:137`).
4. **The result:** a clean hit, a bounce, a blocked swing or a broken parry.

Damage collects through `WP_SaberDamageAdd` (`:3507`) and is applied together by `WP_SaberApplyDamage` (`:3556`), so one swing doesn't double-hit a target across multiple traces.

### Blocking

Blocking is automatic, driven by your defence level and which way you're facing.

- `WP_SaberCanBlock` (`:9282`) decides if the blade can reach the attack.
- `WP_SaberBlockNonRandom` (`:9133`) picks a parry direction from where the hit lands, and the result is a `BLOCKED_` value (`BLOCKED_TOP`, `BLOCKED_UPPER_LEFT`, and so on).
- `WP_SaberStartMissileBlockCheck` (`:5456`) looks ahead for incoming blaster bolts.
- `BLOCKED_PARRY_BROKEN` leaves you exposed, and enough force knocks the saber away.

### Saber locks

When two attacks meet the right way, `WP_SabersCheckLock` (`:1459`) may start a lock: both fighters are stuck in a paired animation and press attack to push.

`PM_SaberLocked` (`bg_saber.c:1343`) advances it, and `PM_SaberLockBreak` (`:1183`) ends it. A strong enough win is a "super break", decided with some randomness:

```c
qboolean superBreak = (strength+pm->ps->saberLockHits > Q_irand(2,4));
```

### Throwing

Throwing spawns a separate saber entity. `saberFirstThrown` (`:7127`) sends it out, `saberBackToOwner` (`:6927`) brings it home, and `saberCheckKnockdown_Thrown` (`:6894`) checks if it gets knocked down in flight. Its damage scales with `FP_SABERTHROW`.

### Saber data

Hilts are data. `.sab` files in `ext_data/sabers` set the model, blade count, colors, stance limits, damage scales and much more. `bg_saberLoad.c` has a keyword table with well over 100 fields (`saberParseKeywords`), so a modder can create a whole new saber without code.

## Force powers

There are 18 powers (`codemp/qcommon/q_shared.h:362`):

| Alignment | Powers |
| --- | --- |
| Neutral | Jump (Levitation), Speed, Push, Pull, Sight, Saber Attack, Saber Defend, Saber Throw |
| Light | Heal, Mind Trick, Protect, Absorb, Team Heal |
| Dark | Grip, Lightning, Rage, Team Force, Drain |

Alignment comes from `forcePowerDarkLight` (`bg_misc.c:222`). Each power has levels 0 to 3 in normal play. MP limits how many points you can spend using a rank, from `FORCE_MASTERY_UNINITIATED` up to Jedi Master, and costs per level are in `bgForcePowerCost` (`bg_misc.c:177`):

| Power | L1 | L2 | L3 |
| --- | --- | --- | --- |
| Push, Pull, Grip | 1 | 3 | 6 |
| Heal, Speed, Drain | 2 | 4 | 6 |
| Lightning, Protect, Sight | 2 | 5 | 8 |
| Mind Trick, Rage, Saber Throw | 4 | 6 | 8 |
| Saber Attack, Saber Defend | 1 | 5 | 8 |

Your build travels as a userinfo string, parsed by `WP_InitForcePowers` (`w_force.c:158`):

```text
"5-1-000000000000000000"
 rank-side-one digit per power
```

Default is `DEFAULT_FORCEPOWERS` in `bg_public.h`.

### The lifecycle

```text
ForceGrip()                 player requested it; checks
  WP_ForcePowerUsable       (:659) known? alive? allowed now?
  WP_ForcePowerAvailable    (:620) enough energy?
  WP_ForcePowerStart        (:872) activate and spend
WP_ForcePowersUpdate        (:4969) every frame, for each client
  WP_ForcePowerRun          (:4135) per-frame effect, drain, timeout
  WP_ForcePowerRegenerate   (:850)  refill the pool
WP_ForcePowerStop           (:3679) clean up
```

Active powers are bits in `ps.fd.forcePowersActive`. All powers draw on `ps.fd.forcePower`, a single pool shared with some saber specials, using `BG_ForcePowerDrain` (`bg_saber.c:55`).

### Notable powers

- **Push and Pull:** `ForceThrow` (`:2914`) is one function with a `pull` flag. It affects players, projectiles and movable objects, and is resisted by the target's own Push level.
- **Lightning:** `ForceLightning` (`:1628`) starts it and `ForceShootLightning` (`:1756`) does the traces.
- **Grip:** `ForceGrip` (`:1374`) locks on, and `DoGripAction` (`:3805`) holds and damages the target.
- **Absorb:** `WP_AbsorbConversion` (`:791`) converts incoming Force attacks into your own Force energy.
- **Who can be targeted:** `ForcePowerUsableOn` (`:543`) handles teams, dueling and protection.

SP's Force code is in `wp_saber.cpp`, and it is more elaborate, since it must work against NPCs and scripted scenes.

## Ranged weapons

Weapons are in `weapon_t` (`codemp/game/bg_weapons.h:31`): stun baton, melee, saber, Bryar pistol, E-11 blaster, disruptor, bowcaster, repeater, DEMP2, flechette, rocket launcher, thermal detonator, trip mine, det pack, concussion rifle, old Bryar, emplaced gun, turret.

`weaponData[]` (`codemp/game/bg_weapons.c`) holds ammo, fire rates and charge values. Almost every weapon has an alt-fire and many can charge.

Firing runs in two places:

1. `PM_Weapon` handles timing and generates an event in shared code.
2. `FireWeapon` in `g_weapon.c` creates the bolt on the server.

Most shots are `G_RunMissile` entities, traveling on trajectories, which is what makes them deflectable. The disruptor is an instant trace. Client visuals are in `cg_weapons` and the `fx_*` files.

## The damage pipeline

`G_Damage` (`codemp/game/g_combat.c:4425`) is the only way health should change. Roughly in order:

```text
damage redirects (vehicle parts and similar)
Rage and protection modifiers
G_LocationBasedDamageModifier   head, torso, limbs (:4277)
knockback: capped at 200, scaled by g_knockback and mass
friendly-fire check through OnSameTeam
Force Protect
CheckArmor                      armor absorbs part (:2926)
subtract health
pain or die callback
```

`ARMOR_PROTECTION` is 0.5. `DAMAGE_NO_PROTECTION` and similar flags skip stages. Radius damage goes through `G_RadiusDamage` (`:5613`).

`mod` (means of death) says what did the killing, and feeds obituaries and special cases.

### Dismemberment

A saber kill can remove a limb. `G_CheckForDismemberment` (`:4149`) decides, and `G_Dismember` (`:3341`) spawns the severed part. On the model, Ghoul2 hides surfaces and enables `_off` caps. `g_dismember` controls this in MP. SP has `g_dismemberment` and `g_dismemberProbabilities`.

### Hit location

`G_GetHitLocation` (`:65`) works from a point on the body. SP's `G_GetHitLocFromSurfName` can use the exact Ghoul2 surface that was hit. Hit-location names are in `hitlocs.h` in SP.

## Duels and timescale

`gDoSlowMoDuel` in `G_RunFrame` (`g_main.c`) drives the slow-motion end of a duel by changing `timescale` over about a second and a half.
