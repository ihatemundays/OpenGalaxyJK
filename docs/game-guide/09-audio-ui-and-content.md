# Audio, UI and Content

## Sound

The sound system is in `client/snd_*`. It started as Quake III's software mixer, and Raven added streaming MP3, dynamic music, ambient sets and lip sync on top. `docs/sound-code-declutter.md` has notes on how tangled it is and plans for cleanup.

### Playing sounds

| File | Job |
| --- | --- |
| `snd_dma.cpp` | Main interface, channels, spatialization |
| `snd_mem.cpp` | Loading and caching |
| `snd_mix.cpp` | Software mixing |
| `snd_mp3.cpp`, `mp3code/` | MP3 decoding |
| `snd_music.cpp` | Dynamic music |
| `snd_ambient.cpp` | Ambient sets |

The core calls in `codemp/client/snd_dma.cpp`:

```c
S_RegisterSound( name );                               // get a handle
S_StartSound( origin, entityNum, channel, sfx, ... );  // :1542
S_AddLoopingSound( entityNum, origin, velocity, sfx ); // :1924
S_Respatialize( listener, head, axis, inwater );       // :2503
S_Update();                                            // :2742
```

`MAX_CHANNELS` is 32. Channels such as `CHAN_VOICE`, `CHAN_WEAPON` and `CHAN_BODY` are logical slots, so a new voice line replaces the old one on that entity, while two entities' voices can overlap.

Almost all sounds ship as MP3: 12,902 in retail, most of them voice lines. Audio is sent to the OS through SDL, or OpenAL with EAX reverb on Windows. Level reverb data is in the `.eal` files (58 in retail).

### Dynamic music

Levels have matched music that crossfades with the action, tracked as `MusicState_e` (`codemp/client/snd_music.h`):

| State | Meaning |
| --- | --- |
| `eBGRNDTRACK_EXPLORE` | Normal movement |
| `eBGRNDTRACK_ACTION` | Combat |
| `eBGRNDTRACK_BOSS` | Final encounter, optional |
| `eBGRNDTRACK_DEATH` | Death flourish, optional |
| `..._ACTIONTRANS0-3`, `..._EXPLORETRANS0-3` | Musical transitions |

`.dms` data (`ext_data/dms.dat`) describes the tracks and their transition points. Game logic only reports the mood, and the music system finds a musically clean moment to switch.

### Ambient sound

`snd_ambient.cpp` reads ambient set definitions and plays environmental background sounds. Sets are general, local to a point, or attached to a brush model. Random sub-waves on a timer keep a place from sounding like a loop.

### Lip sync

SP measures loudness of voice lines against four thresholds (`s_threshold1` to `4`, `code/client/snd_dma.cpp:456`), and uses that to choose a mouth shape. There are no authored visemes in most cases.

## User interface

Menus are text files, not code. There are 89 `.menu` files in retail, with file lists in `ui/menus.txt` and `ui/ingame.txt`.

### A menu definition

The format is a small declarative language, parsed by `ui_shared` (`codemp/ui/ui_shared.c`, `code/ui/ui_shared.cpp`):

```text
menuDef {
    name "ingame_exit"
    rect 0 0 640 480
    itemDef {
        name "yes"
        type ITEM_TYPE_BUTTON
        text "Quit"
        rect 200 200 240 30
        action { uiScript quit }
    }
}
```

| Element | Struct | Role |
| --- | --- | --- |
| Menu | `menuDef_t` (`ui_shared.h:326`) | A screen |
| Item | `itemDef_t` (`:268`) | One control |

Item types come from `ui/menudef.h`: text, button, radio button, checkbox, edit field, combo, list box, model, ownerdraw, numeric field, slider, yes/no, multi, bind and text scroll.

- **Ownerdraw** items ask code to paint something the format can't describe, such as a player preview or Force rank.
- **Feeders** supply list boxes with data like servers, maps or saves.

### Scripting

Event handlers (`action`, `mouseEnter`, `onOpen`) run a small command language through `Item_RunScript`. The verbs are in `commandList` (`codemp/ui/ui_shared.c:2036`):

`fadein`, `fadeout`, `show`, `hide`, `setcolor`, `open`, `close`, `setasset`, `setbackground`, `setitemrectcvar`, `setitembackground`, `setitemtext`, `setitemcolor`, `setitemcolorcvar`, `setitemrect`, `setteamcolor`, `setfocus`, `setplayermodel`, `transition`, `setcvar`, `setcvartocvar`, `exec`, `play`, `playlooped`, `orbit`, `scale`, `disable`, `defer`, `rundeferred`, `transition2`

`uiScript` calls named functions in `ui_main` for anything more involved.

MP compiles `ui_shared.c` into cgame as well (`codemp/cgame/CMakeLists.txt:123`), so its HUD uses the same system. MP's UI is a DLL, while SP's is inside the exe. MP's `UIMENU_` values (`codemp/ui/ui_public.h:39`) let the engine open specific menus.

The UI needs to run with no map loaded, so it has its own frame path: `SCR_UpdateScreen` calls `UIVM_Refresh` (`codemp/client/cl_uiapi.cpp:92`) when the UI is active. That's also why a stale UI module fails right after startup.

## Text and languages

User-facing text isn't in code. Files in `strings/<language>/*.str` hold translations by reference name, and the StringEd system in `stringed_ingame.cpp` looks them up through `SE_GetString`. There are 215 `.str` files in retail. See `docs/language strings.md`.

## Retail content

The retail game ships in five archives under `GameData/base`: `assets0.pk3` to `assets4.pk3`. They contain roughly 23,800 files.

### By folder

| Folder | Files | Contents |
| --- | --- | --- |
| `sound/` | 13,072 | Voice, effects, ambience |
| `models/` | 3,460 | Characters, weapons, props |
| `textures/` | 2,310 | World and model art |
| `gfx/` | 1,251 | HUD, menus, effects sprites |
| `scripts/` | 1,114 | ICARUS and `.arena` files |
| `effects/` | 376 | `.efx` files |
| `ext_data/` | 241 | Sabers, NPCs, vehicles, Siege data |
| `strings/` | 215 | Text |
| `music/` | 133 | Music tracks |
| `ui/` | 117 | Menu lists and art |
| `maps/` | 94 | `.bsp` levels and companion data |
| `menu/` | 88 | Menu art and data |
| `shaders/` | 68 | `.shader` files |
| `fffx/` | 80 | Force feedback, `.ifr` |
| `levelshots/` | 56 | Loading images |
| `eagle/` | 58 | EAX `.eal` data |
| `botfiles/`, `botroutes/` | 38, 20 | Bot personalities and waypoints |
| `fonts/` | 32 | Font data |
| `video/` | 14 | ROQ cinematics |
| `forcecfg/` | 10 | Force templates, `.fcf` |

### File types worth knowing

| Extension | What it is | Loaded by |
| --- | --- | --- |
| `.bsp` | Compiled level | `cm_load`, `tr_bsp` |
| `.glm`, `.gla` | Ghoul2 mesh, skeleton | Renderer |
| `.md3` | Static or vertex model | Renderer |
| `.skin` | Surface to shader map | Renderer |
| `.shader` | Material | `tr_shader` |
| `.efx` | Particle effect | `FxScheduler` |
| `.ibi` | Compiled ICARUS script | ICARUS |
| `.rof` | Recorded motion | `RoffSystem` |
| `.npc` | NPC definitions | `NPC_stats` |
| `.sab` | Saber definitions | `bg_saberLoad` |
| `.veh`, `.vwp` | Vehicles and their weapons | `bg_vehicleLoad` |
| `.scl`, `.team` | Siege classes and teams | `bg_saga` |
| `.menu` | UI definition | `ui_shared` |
| `.str` | Text strings | StringEd |
| `.nav` | NPC navigation | Navigation |
| `.wnt` | Bot waypoints | `ai_wpnav` |
| `.roq` | Video | `cl_cin` |
| `.arena` | Map and mode lists | UI and game |
| `.cfg` | Console scripts, `animation.cfg`, `animevents.cfg` | Engine, game |
| `.fontdat` | Font metrics | `tr_font` |

The whole game favors data-driven design. A surprising amount can be changed with no compiler:

| To change | Edit |
| --- | --- |
| Saber stats, hilt or blade | `.sab` |
| An NPC's stats or weapon | `.npc` |
| Weapon sounds, muzzle effect, models | `ext_data/weapons.dat` |
| An effect's look | `.efx` |
| A cutscene | ICARUS source, recompiled to `.ibi` |
| A menu | `.menu` |
| Move timing | `animation.cfg`, which affects gameplay |

To test a change, put a pk3 with a late-sorting name such as `zzz_mymod.pk3` in `base`. It overrides retail files, and you can see this approach already in community mods like `zzz_custom_character_creator_upgrade.pk3`.

Code changes, on the other hand, can change wire formats or module APIs. Keep the rules from [Simulation and networking](02-simulation-and-networking.md) in mind, and rebuild every module that shares what you changed.
