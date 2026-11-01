# Architecture

## Programs and modules

The game is an engine executable plus libraries it loads at runtime. Quake III ran game code in a sandboxed bytecode VM. Jedi Academy kept the VM naming but runs native DLLs, so "VM" in this code just means "a loaded module".

### Multiplayer

| File | Source | Role |
| --- | --- | --- |
| `openjk.x86_64` | `codemp/client`, `server`, `qcommon`, `botlib` | Client engine, which also contains a listen server |
| `openjkded.x86_64` | same, without the client | Dedicated server |
| `jampgamex86_64` | `codemp/game` | Server game: rules, entities, combat, NPCs |
| `cgamex86_64` | `codemp/cgame` | Client game: prediction, HUD, drawing the world |
| `uix86_64` | `codemp/ui` | Menus |
| `rd-vanilla_x86_64` | `codemp/rd-vanilla` | Default OpenGL renderer |
| `rd-rend2_x86_64` | `codemp/rd-rend2` | Experimental modern renderer |

### Single player

| File | Source | Role |
| --- | --- | --- |
| `openjk_sp.x86_64` | `code/client`, `server`, `qcommon`, `ui` | Engine. The UI is compiled in here (`code/CMakeLists.txt:303`) |
| `jagamex86_64` | `code/game` **and** `code/cgame` | Game and cgame in one DLL (`code/game/CMakeLists.txt:190`) |
| `rdsp-vanilla_x86_64` | `code/rd-vanilla` | Renderer |

There is no SP rend2 and no SP dedicated server. Jedi Outcast targets exist but `BuildJK2SPEngine` is `OFF` by default.

## How modules talk

Modules never call engine functions directly. Each side hands the other a table of function pointers.

```text
Engine                                 Module
  VM_Create -> Sys_LoadGameDll  ---->  GetModuleAPI(version, import)
  import table (engine services) --->  stored as "trap"
  <--- export table (Init, RunFrame, ...)
```

- **MP modules** export `GetModuleAPI` (`codemp/game/g_main.c:3569`). Game code calls the engine through `trap->...`, for example `trap->Cvar_Set`.
- **Legacy MP mods** export `vmMain` and `dllEntry`, a single dispatcher with numbered commands. The engine still supports them (`shared/sys/sys_main.cpp:469`), so old mods run.
- **SP game** exports `GetGameAPI`, with `gi.` as its import table (`code/server/sv_game.cpp:1058`).
- **SP cgame** uses the older `vmMain`/`dllEntry` pair from the same DLL (`code/client/cl_cgame.cpp:66`).
- **Renderers** export `GetRefAPI`. `refexport_t` has about 157 entries and `REF_API_VERSION` is 9 (`codemp/rd-common/tr_public.h:33`).

These tables are binary contracts. If the engine and a module disagree about one, the result is usually a crash through a bad function pointer, not a clean error. See `docs/libraries.md` for more detail.

The server also needs the renderer. Ghoul2 skeletons live in the renderer library, and the game uses them for hit detection. A dedicated server has no GPU, so `codemp/rd-dedicated` is a cut-down renderer holding just the model and Ghoul2 code.

## Startup

`main` (`shared/sys/sys_main.cpp:745`) resolves paths, joins the command line and calls `Com_Init`, then loops on `Com_Frame` forever.

`Com_Init` (`codemp/qcommon/common.cpp:1155`) brings systems up in dependency order:

1. Event queue and zone memory
2. Cvars, then the command line, so `+set` values apply before anything reads them
3. The command system
4. The filesystem, which is why `fs_basepath` has to be on the command line
5. `mpdefault.cfg` or `default.cfg`, then the saved config, then `autoexec.cfg`
6. Network, server and client
7. Remaining startup commands, or the logo cinematic if there were none

If the defaults can't be loaded the game stops here, which is the `Couldn't load default.cfg` failure.

## The main loop

`Com_Frame` (`codemp/qcommon/common.cpp`) is the heart of the program:

```text
Com_Frame
  sleep until the next frame is due (com_maxfps)
  IN_Frame            poll SDL input into the event queue
  Com_EventLoop       dispatch keys, mouse, network packets
  Cbuf_Execute        run queued console commands
  SV_Frame(msec)      advance the server simulation
  Com_EventLoop       collect server packets without a frame of latency
  Cbuf_Execute
  CL_Frame(msec)      send input, render, update audio
```

The server runs in fixed steps while the client runs as fast as it can. `SV_Frame` banks elapsed time and consumes it in `1000 / sv_fps` chunks:

```c
while ( sv.timeResidual >= frameMsec ) {
    sv.timeResidual -= frameMsec;
    sv.time += frameMsec;
    GVM_RunFrame( sv.time );
}
```

Then it sends snapshots to clients (`codemp/server/sv_main.cpp:1145`). `CL_Frame` sends player input, works out which server time to show, and calls `SCR_UpdateScreen` and `S_Update`.

SP's `SV_Frame` and `CL_Frame` take a second fractional-millisecond argument (`code/server/sv_main.cpp:431`), which keeps time smooth at high frame rates.

## Foundation services

### Cvars and commands

Cvars are named console variables, and nearly every tunable in the game is one. Flags control their behaviour (`codemp/qcommon/q_shared.h:706`):

| Flag | Meaning |
| --- | --- |
| `CVAR_ARCHIVE` | Saved to the config file |
| `CVAR_ARCHIVE_ND` | Saved only when it differs from the default |
| `CVAR_USERINFO` | Sent to the server, used for name and model |
| `CVAR_SERVERINFO` | Shown to server browsers |
| `CVAR_SYSTEMINFO` | Copied to all clients |
| `CVAR_LATCH` | Takes effect only on a restart of that system |
| `CVAR_CHEAT` | Locked unless cheats are on |

### Filesystem

`codemp/qcommon/files.cpp` merges folders and `.pk3` files, which are ordinary zip archives, into one virtual tree. A search stops at the first hit. For each root it checks pk3s before loose files, and later pk3s beat earlier ones, so a `zzz_` pk3 overrides retail data.

```text
home path + fs_game
base path + fs_game
home path + base
base path + base
```

`fs_basepath` holds the install and `fs_homepath` holds anything written. Every asset access goes through here, so game data can be overridden without touching code.

### Memory

- **Zone** (`Z_Malloc`) is a tagged heap. Tags in `tags.h` let a whole category be freed at once, and `TAG_` values appear throughout.
- **Hunk** (`Hunk_Alloc`) is level-lifetime memory, cleared in one go by `Hunk_Clear` when a map changes.
- **Module pools:** game code keeps fixed arrays and its own pool (`g_mem`), not a general-purpose heap. Limits like `MAX_GENTITIES` are hard caps.

### Errors

`Com_Error` throws. `ERR_DROP` ends the current game and returns to the menu, while `ERR_FATAL` quits. `Com_Frame` wraps its body in a `try` for this reason. Crash logs land in the home path as `crashlog-<date>.txt`.

### Platform layer

`shared/sys` has the OS code and `shared/sdl` has windowing, input and audio output. `SDL_CreateWindow` is in `shared/sdl/sdl_window.cpp:574`, and input becomes engine events through `Sys_QueEvent` in `shared/sdl/sdl_input.cpp`.
