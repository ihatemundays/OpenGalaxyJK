# Rendering and Models

## Renderers

The renderer is a swappable library, chosen by `cl_renderer` in `CL_InitRef` (`codemp/client/cl_main.cpp:2417`).

| Renderer | Tech | Status |
| --- | --- | --- |
| `rd-vanilla` / `rdsp-vanilla` | Fixed-function OpenGL, as shipped | Default |
| `rd-rend2` | GLSL shaders, PBR-style lighting, post-processing | MP only, marked experimental in CMake |
| `rd-dedicated` | No drawing, just models and Ghoul2 | Dedicated server |

`rd-common` holds shared code: image loaders and the font system.

## One frame

Game code never touches OpenGL. It fills a scene and asks for it to be drawn. See also `docs/renderer-architecture.md`.

```text
cgame                         renderer front end              back end
ClearScene
AddRefEntityToScene  ------>
AddPolyToScene, AddLightToScene
RenderScene          ------>  RE_RenderScene
                              R_RenderView
                              R_GenerateDrawSurfs
                                 walk the BSP, cull
                                 R_AddEntitySurfaces
                              R_AddDrawSurf for each surface
                              R_SortDrawSurfs
                              issue a draw command  ------->  RB_ExecuteRenderCommands
                                                              RB_RenderDrawSurfList
                                                              RB_StageIteratorGeneric
                                                              GL calls
```

Key places in `codemp/rd-vanilla`:

| Step | Function |
| --- | --- |
| Start a scene | `RE_RenderScene` (`tr_scene.cpp:397`) |
| Gather surfaces | `R_GenerateDrawSurfs` (`tr_main.cpp:1314`) |
| Queue one surface | `R_AddDrawSurf` (`tr_main.cpp:1094`) |
| Sort | `R_SortDrawSurfs` (`tr_main.cpp:1137`) |
| Run commands | `RB_ExecuteRenderCommands` (`tr_backend.cpp:1956`) |
| Draw a list | `RB_RenderDrawSurfList` (`tr_backend.cpp:673`) |
| Batch geometry | `RB_BeginSurface`, `RB_EndSurface` (`tr_shade.cpp:355`, `:1926`) |

**The sort is the core optimization.** Each draw surface packs its shader index and other state into one integer (`tr_local.h:887`), so sorting is fast. Surfaces with the same shader then batch together, which limits GL state changes.

Commands go through a buffer between front end and back end, which is how Quake III's multithreaded design worked.

## Shaders

A "shader" here isn't a GPU program. It's a text material definition in a `.shader` file (68 in retail) that describes a surface as stacked texture passes:

```text
textures/example/forcefield
{
    cull disable
    {
        map textures/example/field.jpg
        blendFunc GL_ONE GL_ONE
        tcMod scroll 0 1
        rgbGen wave sin 0.5 0.2 0 1
    }
}
```

`tr_shader.cpp` parses these. A shader can have up to `MAX_SHADER_STAGES` (8) stages, and animate color and texture coordinates over time.

The `sort` value decides draw order: `SS_OPAQUE`, `SS_DECAL`, `SS_UNDERWATER`, `SS_BLEND0`, and so on (`tr_local.h:168`). Transparent surfaces have to be drawn after opaque ones.

If no shader exists for a name, the engine auto-generates one from a texture of the same name, using a lightmap in the world.

### Raven's additions

- **Multiple lightmaps and light styles:** up to `MAXLIGHTMAPS` (4) per surface.
- **Dynamic glow:** a post-process bloom on marked stages, with `r_DynamicGlow`.
- **Surface sprites:** grass and other detail billboards generated on surfaces (`tr_surfacesprites.cpp`, `SURFSPRITE_` types in `tr_local.h:359`).
- **Weather:** `tr_WorldEffects.cpp` renders rain, snow, fog and wind.
- **Terrain and decals:** `tr_terrain.cpp`, `tr_decals.cpp`.
- **Surface materials:** materials like metal or sand change footsteps and impacts, using the `MATERIAL_` values.

## Models

| Format | Extension | Count in retail | Used for |
| --- | --- | --- | --- |
| MD3 | `.md3` | 595 | Rigid and vertex-animated props, items, world weapons |
| Ghoul2 mesh | `.glm` | 127 | Skinned characters and complex models |
| Ghoul2 animation | `.gla` | 66 | Skeletons and animation |

`.skin` files (305) map model surfaces to shaders, so one mesh can have many looks. rend2 can also load IQM models (`tr_model_iqm.cpp`).

## Ghoul2

Ghoul2 is Raven's skeletal animation system, and it's a gameplay system as much as a graphics one. The code sits in the renderer library: `G2_API.cpp`, `G2_bones.cpp`, `G2_bolts.cpp`, `G2_surfaces.cpp`, `G2_misc.cpp` and `tr_ghoul2.cpp`.

### File format

Meshes and skeletons are separate files (`rd-common/mdx_format.h`):

- A `.glm` mesh has the magic `2LGM` and refers to a skeleton.
- A `.gla` file has the magic `2LGA`, and many meshes can share one, such as `_humanoid.gla`.

Sharing is why the same animations work on most human characters.

### Instances

An entity holds a `CGhoul2Info_v`, a list of models attached to each other. A character might be body, saber and a held item. Each `CGhoul2Info` (`codemp/ghoul2/ghoul2_shared.h`) keeps three lists:

| List | Holds |
| --- | --- |
| `mBlist` | Bone overrides: playing animations and forced angles |
| `mSlist` | Surface overrides: visible or hidden, including generated surfaces |
| `mBltlist` | Bolts: attachment points being tracked |

### What it can do

| Feature | API | Used for |
| --- | --- | --- |
| Per-bone animation | `G2API_SetBoneAnim` | Split legs and torso, with blending |
| Per-bone angles | `G2API_SetBoneAngles` | Head and torso aim, looking at things |
| Bolts | `G2API_GetBoltMatrix` | Finding the saber hand, muzzle or foot |
| Show or hide surfaces | `G2API_SetSurfaceOnOff` | Dismemberment, swapping parts |
| Skeleton collision | `G2API_CollisionDetect` | Per-limb hit detection |
| Gore | `G2_gore`, `ghoul2_gore.h` | Saber marks on a body |
| Ragdoll and IK | `BONE_ANGLES_RAGDOLL`, `BONE_ANGLES_IK` in `G2.h` | Death physics |

Skeletons are computed lazily by `G2_ConstructGhoulSkeleton`, since evaluating every bone on every entity is expensive. A skeleton is built only when something asks: drawing it, or a game query for a bolt.

The server has to evaluate animation too, because saber positions decide combat. That makes Ghoul2 a real server cost, and it's why dedicated servers carry `rd-dedicated`. The ragdoll code in `G2_bones.cpp` uses a callback into the client for collisions.

Time for all this comes from `G2API_SetTime`, which is set from both server and client frames.

### Dismemberment in the model

Art includes hidden `_off` surfaces, so cutting an arm doesn't just hide it: the stump appears as a modeled cap. `G2_SetRootSurface` and new-model logic make a severed limb its own entity.

## The effects system

`.efx` files (376 in retail, under `effects/`) define particle effects. Designers built these in an editor, and code only says "play this effect here".

```cpp
theFxScheduler.PlayEffect( "saber/blood_sparks", origin, normal );
```

The code is in `code/cgame/Fx*` for SP, and in `codemp/client/Fx*` in the engine for MP, since cgame reaches it with `trap->FX_*`.

| Class | Job |
| --- | --- |
| `CFxScheduler` | Parses templates and spawns timed primitives (`FxScheduler.h:614`) |
| `SEffectTemplate`, `CPrimitiveTemplate` | The parsed definition |
| `CEffect` and subclasses | Live objects (`FxPrimitives.h`) |
| `FxUtil` | Per-frame update and draw |

An effect is up to `FX_MAX_EFFECT_COMPONENTS` (24) components, each with randomized value ranges (`CFxRange`). Primitive types: `Particle`, `Line`, `Tail`, `Cylinder`, `Emitter`, `Sound`, `Decal`, `OrientedParticle`, `Electricity`, `Light`, `CameraShake`. Trails, bezier curves and polys are in there too.

Effects can stick to a Ghoul2 bolt, so sparks stay on a hand. `FX_MAX_EFFECTS` caps the number of loaded templates at 150 in SP and 256 in MP. Allocation comes from fixed pools, since many short-lived particles spawn each frame.

### Saber drawing

The blade itself is built in cgame. `CG_AddSaberBlade` draws the glow and core, and the swoosh trail comes from recent blade positions. Saber marks on walls are decals, added by `CG_ImpactMark` and similar.

## 2D drawing and fonts

`DrawStretchPic` and related calls handle the HUD and menus. Coordinates are in a virtual 640 by 480 space.

`.fontdat` files (7 in retail) describe bitmap fonts. Asian languages need a separate path through `Language_IsAsian`.
