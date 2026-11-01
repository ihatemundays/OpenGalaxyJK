# RmlUi menu migration

Replaces every retail `.menu` file with RmlUi documents, in SP and MP. See `menu-inventory.md` for what the retail menus use.

## Layout

| Path | Role |
| --- | --- |
| `shared/rml/rml_ui.cpp` | RmlUi runtime: drawing, files, fonts, menu scripts |
| `shared/rml/cl_rml.inl` | Client side: commands, cvar, input |
| `code/client/cl_rml.cpp`, `codemp/client/cl_rml.cpp` | SP and MP entry points |
| `tools/menu_converter/` | `.menu` parser, inventory and RML translator |

RmlUi runs in the client exe, not the MP ui module, so the module API is unchanged. CMake downloads RmlUi 6.3 on the first configure.

## Generating the menus

The output comes from retail data, so it goes in the game folder and not in this repo:

```text
py -3 tools/menu_converter/inventory.py "<GameData>/base" docs/ui-migration
py -3 tools/menu_converter/menu_to_rml.py "<GameData>/base" "<profile>/OpenJK/base"
```

## Commands

| Command | Effect |
| --- | --- |
| `set ui_rmlUi 0` | Go back to the legacy menus. It defaults to 1, and menus are blank if the generated files are missing |
| `rml_list` | List the menus for this exe |
| `rml_open <menu>` | Open a menu by name |
| `rml_close [menu]` | Close one menu, or all of them |
| `rml_reload` | Reload the files of open menus after an edit |
| `rml_click <item> [x] [y]` | Click a menu item by name from a script, at a fraction of its size |
| `rml_drag <item> <from> <to>` | Drag a slider from a script, between fractions of its width |

`rml_sync`, `rml_run` and `rml_paint` are internal. Files are in `ui/rml/sp`, `ui/rml/mp` and `ui/rml/base.rcss`.

## How actions reach the game

The legacy ui code still owns game logic, and RmlUi draws the menus:

1. With `ui_rmlUi` on, the ui code stops painting and sends its list of open menus with `rml_sync`, so RmlUi follows every menu the game opens or closes.
2. A click sends `rml_run <menu> <item index> click`. `Menu_RmlEvent` runs it on the real legacy item like `Menu_HandleKey`: the item takes the key, then its action runs. That covers cvars, sounds, `uiScript` calls, and yes/no and multi items.
3. RmlUi then applies the same script for what it changes on screen, such as `show`, `hide`, `setitemcolor` and `setitemrect`. Transitions and fades jump to their end state.
4. The keyboard stays with the legacy code, which runs `onESC`.
5. Each document ends with an image named `rmlpaint/<menu>`. It isn't art: when the renderer reaches it, `rml_paint <menu>` has the legacy code paint the items RmlUi has no widget for: models, lists, sliders, ownerdraws, text scrollers and key bindings. That keeps them in the menu's place in the stack, so popups cover them. Documents hold an invisible area for each so the mouse can reach them. Clicks and the wheel carry the cursor position, so lists pick the right row.

Those items keep their retail look and can't be styled from RML until they get native widgets. Menus stretch to the window like the legacy UI, so the two layers stay lined up.

Removing the legacy system later means moving this logic out of `ui_shared` and `ui_main`, not just deleting the `.menu` files.

Two rules keep this safe. `Cmd_Argv` and `UI_Argv` results are overwritten by the next command, so copy them first. Menu events run only during mouse input, and `Host_Run` restores the current command's arguments, because anything run inside another command, such as `map`, would otherwise overwrite them.

The MP `rml_run` command lives in the ui module, so rebuild `uix86_64` and copy it to the profile `base` folder after changing it. The game loads that copy first.

## Limits

- Art and text go through `DrawStretchPic`, so geometry is limited to axis-aligned quads. Rounded corners, gradients and transforms won't draw correctly.
- `GenerateTexture` isn't implemented, because no renderer call uploads raw pixels. RmlUi's SVG plugin needs it.
- Images load through the shader system, so names without an image file also work, including animated shaders.
- Fonts are the retail bitmap atlases read from `.fontdat`.
- Multi items filled from a feeder have runtime labels: species use `MENUS_<name>`, and languages use `MENUS_MYLANGUAGE`.
- `rml_open` previews don't show legacy-painted items, because those need the menu open in the game.
