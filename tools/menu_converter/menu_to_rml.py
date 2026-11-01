"""Translate retail .menu files directly into RmlUi documents.

Usage: py -3 menu_to_rml.py <GameData/base> <output base folder>

Writes <output>/ui/rml/base.rcss, ui/rml/sp/<menu>.rml and ui/rml/mp/<menu>.rml.
Layout, art and text carry over. A document keeps each item's position in its
menu, and a click runs that item's original script in the ui code, so game logic
stays in one place. Scripts are kept here for what they change on screen. Widgets
that need game data (lists, models, ownerdraws, sliders, bindings) are painted by
the ui code over the document, which only holds an invisible area for the mouse.

The output is derived from retail data, so it goes in the game folder, not the repo.
"""
from __future__ import annotations

import json
import sys
from html import escape
from pathlib import Path

from inventory import ITEM_TYPES, game_files, item_type, read
from menu_parser import parse_text

POINT_SIZE = {"aurabesh": 18, "ergoec": 20, "anewhope": 17, "arialnb": 14, "ocr_a": 18}
FONTS = {"0": "ergoec", "1": "aurabesh", "2": "ergoec", "3": "anewhope", "4": "arialnb"}
STYLES = {"WINDOW_STYLE_EMPTY": 0, "WINDOW_STYLE_FILLED": 1, "WINDOW_STYLE_GRADIENT": 2,
          "WINDOW_STYLE_SHADER": 3, "WINDOW_STYLE_TEAMCOLOR": 4, "WINDOW_STYLE_CINEMATIC": 5}
ALIGN = {"ITEM_ALIGN_LEFT": 0, "ITEM_ALIGN_CENTER": 1, "ITEM_ALIGN_RIGHT": 2}
LEGACY_PAINTED = {"LISTBOX", "MODEL", "OWNERDRAW", "SLIDER", "TEXTSCROLL", "BIND"}
CLICKABLE = {"YESNO", "MULTI", "EDITFIELD", "NUMERICFIELD", "BIND", "LISTBOX", "SLIDER", "OWNERDRAW", "TEXTSCROLL"}
VALUE_TYPES = {"YESNO", "MULTI", "EDITFIELD", "NUMERICFIELD"}
FONT_HEIGHT = {"aurabesh": 18, "ergoec": 22, "anewhope": 20, "arialnb": 14, "ocr_a": 21}
GRADIENT_BAR = "ui/assets/gradientbar2"  # GradientBar_Paint's art
HOVER_SCRIPTS = (("action", "data-action"), ("mouseenter", "data-mouseenter"), ("mouseexit", "data-mouseexit"))

BASE_RCSS = """body {
	display: block;
	position: absolute;
	left: 0;
	top: 0;
	width: 100%;
	height: 100%;
	font-family: ergoec;
	font-size: 11dp;
	color: #ffffff;
}
div, span { display: block; }
.screen {
	position: absolute;
	left: 50%;
	top: 0;
	width: 640dp;
	height: 480dp;
	margin-left: -320dp;
}
.item { position: absolute; box-sizing: border-box; }
#rmlpaint {
	position: absolute;
	left: 0;
	top: 0;
	width: 1dp;
	height: 1dp;
	pointer-events: none;
}
#rmldesc {
	position: absolute;
	font-family: arialnb;
	white-space: nowrap;
	pointer-events: none;
}
.decoration { pointer-events: none; }
.item span { position: absolute; white-space: nowrap; pointer-events: none; }
.item span.wrap { white-space: normal; }
.item span span { position: static; display: inline; margin-left: 8dp; }
.hidden, .cvarhidden { display: none; }
.disabled { opacity: 0.5; }
"""


def num(value, default=0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def dp(value: float) -> str:
    return f"{value:g}dp"


def color(value) -> str | None:
    if not value or len(value) < 4:
        return None
    r, g, b, a = (max(0, min(255, round(num(c) * 255))) for c in value)
    return f"#{r:02x}{g:02x}{b:02x}{a:02x}"


def enum(value, table) -> int:
    return table[value] if value in table else int(num(value))


def script(commands) -> str:
    return ";".join(" ".join(str(a) for a in cmd) for cmd in commands or [])


def attr(name, value) -> str:
    return f' {name}="{escape(str(value), quote=True)}"'


def value_attrs(item) -> str:
    out = attr("data-cvar", item.get("cvar"))
    if item_type(item) == "YESNO":
        return out + attr("data-yesno", "invert" if str(item.get("invertyesno", "0")) != "0" else "on")
    choices = item.get("cvarstrlist") or item.get("cvarfloatlist")
    if choices == ["feeder"]:
        species = str(item.get("feeder")) in ("16", "FEEDER_PLAYER_SPECIES")
        return out + (attr("data-multiprefix", "@MENUS_") if species else attr("data-multilabel", "@MENUS_MYLANGUAGE"))
    if choices and len(choices) > 1:
        out += attr("data-multi", ";".join(choices))
    return out


def text_spans(item, width: float, border: float) -> str:
    kind = item_type(item)
    text, cvar = item.get("text"), item.get("cvar") if kind == "TEXT" else None
    value = value_attrs(item) if kind in VALUE_TYPES and item.get("cvar") else None
    if not text and not cvar and not value:
        return ""
    scale = num(item.get("textscale"), 0.55)
    family = FONTS.get(str(item.get("font", "0")), "ergoec")
    size = POINT_SIZE[family] * scale
    x, y = num(item.get("textalignx")) + border, num(item.get("textaligny")) + border
    fore = color(item.get("forecolor")) or "#ffffffff"
    align = enum(item.get("textalign", 0), ALIGN)
    wrapped = bool(item.get("autowrapped"))

    style = f"top:{dp(y)};font-family:{family};font-size:{dp(size)};"
    if wrapped:  # the wrap width comes from the item, the alignment point from textalignx
        style += f"left:0;width:{dp(width)};text-align:{'center' if align == 1 else 'left'};"
    elif align == 1:
        style += f"left:{dp(x - 1000)};width:2000dp;text-align:center;"
    elif align == 2:
        style += f"left:{dp(x - 2000)};width:2000dp;text-align:right;"
    else:
        style += f"left:{dp(x)};"

    classes = (["wrap"] if wrapped else []) + (["cvartext"] if cvar and not text else [])
    css_class = attr("class", " ".join(classes)) if classes else ""
    binding = attr("data-cvar", cvar) if cvar and not text else ""
    body = escape(text or "")
    out = ""
    value_span = ""
    if value:  # the value follows the label, or sits at xoffset when there's no label
        if text and align != 2:
            body += f'<span class="cvartext"{value}></span>'
        else:
            left = x + 8 if text else x + num(item.get("xoffset"))
            value_span = (f'<span class="cvartext"{value} style="top:{dp(y)};font-family:{family};'
                          f'font-size:{dp(size)};left:{dp(left)};color:{fore};"></span>')
    if enum(item.get("textstyle", 0), {}) >= 3:  # RE_Font_DrawString's drop shadow
        offset = round(POINT_SIZE[family] * scale * 0.075)
        shadow = style.replace(f"top:{dp(y)}", f"top:{dp(y + offset)}", 1)
        shadow = shadow.replace(f"left:{dp(x)}", f"left:{dp(x + offset)}", 1)
        out += f'<span{css_class}{binding} style="{shadow}color:#262626ff;">{escape(text or "")}</span>'
    if text or cvar:
        out += f'<span{css_class}{binding} style="{style}color:{fore};">{body}</span>'
    return out + value_span


def item_rml(item, index, stats) -> str:
    rect = [num(v) for v in (item.get("rect") or [0, 0, 0, 0])]
    border_on = num(item.get("border")) != 0
    border = num(item.get("bordersize"), 1) if border_on else 0
    kind = item_type(item)
    style_id = enum(item.get("style", 0), STYLES)
    x, y, w, h = rect[0] + border, rect[1] + border, rect[2], rect[3]

    css = f"left:{dp(x)};top:{dp(y)};width:{dp(w)};height:{dp(h)};"
    background = item.get("background")
    painted = kind in LEGACY_PAINTED
    if painted:
        border_on = False
    elif style_id == 3 and background:
        css += f'decorator:image("{background}");'
        fore = item.get("forecolor")
        if fore and len(fore) == 4:
            css += f"image-color:{color(fore)};"
    elif style_id in (1, 2) and color(item.get("backcolor")):
        css += f"background-color:{color(item.get('backcolor'))};"
    if border_on:
        css += f"border:{dp(border)} {color(item.get('bordercolor')) or '#ffffffff'};"

    classes = ["item"]
    if str(item.get("visible", "1")) == "0":
        classes.append("hidden")
    interactive = kind in CLICKABLE or any(item.get(k) for k, _ in HOVER_SCRIPTS)
    if item.get("decoration") or not interactive:
        classes.append("decoration")
    inner = "" if painted else text_spans(item, w, border)
    if painted:
        stats[kind] = stats.get(kind, 0) + 1
    if kind == "SLIDER":
        classes.append("slider")

    rule = next(((name, item.get(key)) for key, name in CVAR_RULES if item.get(key)), None)
    cvar_attrs = ""
    if rule and item.get("cvartest"):
        classes.append("cvarstate")
        cvar_attrs = (attr("data-cvartest", item.get("cvartest")) + attr("data-cvarrule", rule[0])
                      + attr("data-cvarvalues", ";".join(rule[1])))

    attrs = attr("class", " ".join(classes)) + attr("style", css) + attr("data-index", index) + cvar_attrs
    for key in ("name", "group"):
        if item.get(key):
            attrs += attr(f"data-{key}", item.get(key))
    if item.get("desctext") and "decoration" not in classes:
        attrs += attr("data-desc", item.get("desctext"))
    for key, name in HOVER_SCRIPTS:
        if item.get(key):
            attrs += attr(name, script(item.get(key)))
    return f"<div{attrs}>{inner}</div>"


def menu_rml(menu, stats) -> str:
    rect = [num(v) for v in (menu.get("rect") or [0, 0, 640, 480])]
    border = num(menu.get("bordersize"), 1) if num(menu.get("border")) else 0
    focus = color(menu.get("focuscolor")) or "#ffffffff"
    fullscreen = str(menu.get("fullscreen", "0")) != "0"
    title = escape(menu.get("name", "menu"))
    align = enum(menu.get("descalignment", 0), ALIGN)
    desc_x, desc_size = num(menu.get("descx")), POINT_SIZE["arialnb"] * (num(menu.get("descscale")) or 1)
    desc_left = {0: desc_x, 1: desc_x - 1000, 2: desc_x - 2000}[align]
    desc_style = (f"left:{dp(desc_left)};top:{dp(num(menu.get('descy')))};font-size:{dp(desc_size)};"
                  f"text-align:{('left', 'center', 'right')[align]};color:{color(menu.get('desccolor')) or '#ffffffff'};"
                  + ("width:2000dp;" if align else ""))
    body_style = "background-color:#000000ff;" if fullscreen else ""
    items = "\n".join(item_rml(item, i, stats) for i, item in enumerate(menu.items))
    data = (attr("data-menu", menu.get("name", "menu")) + attr("data-x", rect[0] + border)
            + attr("data-y", rect[1] + border) + attr("data-onopen", script(menu.get("onopen"))))
    return f"""<rml>
<head>
<title>{title}</title>
<link type="text/rcss" href="ui/rml/base.rcss"/>
<style>.item:hover span {{ color: {focus}; }}</style>
</head>
<body style="{body_style}"{data}>
<div class="screen">
<div style="position:absolute;left:{dp(rect[0] + border)};top:{dp(rect[1] + border)};width:{dp(rect[2])};height:{dp(rect[3])};">
{items}
</div>
<div id="rmlpaint" style="decorator:image(&quot;rmlpaint/{title}&quot;);"></div>
<div id="rmldesc" style="{desc_style}"></div>
</div>
</body>
</rml>
"""


def main():
    base, out = Path(sys.argv[1]), Path(sys.argv[2])
    files = game_files(base)
    root = out / "ui" / "rml"
    root.mkdir(parents=True, exist_ok=True)
    (root / "base.rcss").write_text(BASE_RCSS, encoding="utf-8")

    report = {}
    for label, prefix, depth in (("sp", "ui/", 2), ("mp", "ui/jamp/", 3)):
        target = root / label
        target.mkdir(exist_ok=True)
        for old in target.glob("*.rml"):
            old.unlink()
        stats, count = {}, 0
        for path in sorted(files):
            if path.endswith(".menu") and path.startswith(prefix) and path.count("/") == depth - 1:
                parsed = parse_text(read(files[path]), path)
                for menu in parsed.menus:
                    name = str(menu.get("name", Path(path).stem)).lower()
                    (target / f"{name}.rml").write_text(menu_rml(menu, stats), encoding="utf-8")
                    count += 1
        report[label] = {"menus": count, "painted_by_ui_code": stats}
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
