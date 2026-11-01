"""Parser for Jedi Academy .menu files.

Follows the keyword tables in code/ui/ui_shared.cpp and codemp/ui/ui_shared.c,
so a keyword the engine would not recognise is reported, not silently dropped.
"""
from __future__ import annotations

from dataclasses import dataclass, field

STRING, WORD, PUNCT = "string", "word", "punct"


@dataclass
class Token:
    kind: str
    value: str
    line: int


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    i, line, n = 0, 1, len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c.isspace():
            i += 1
        elif text.startswith("//", i) or text.startswith("\\\\", i) or c == "#":
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += text.count("\n", i, end)
            i = end
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 1
            tokens.append(Token(STRING, text[i + 1 : j], line))
            line += text.count("\n", i, j)
            i = j + 1
        elif c in "{};":
            tokens.append(Token(PUNCT, c, line))
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '{};"':
                j += 1
            tokens.append(Token(WORD, text[i:j], line))
            i = j
    return tokens


# "script" is a command block, "list" a braced value list, numbers a fixed count.
ITEM_ARGS = {
    "accept": "script", "action": "script", "doubleclick": "script", "leavefocus": "script",
    "mouseenter": "script", "mouseentertext": "script", "mouseexit": "script",
    "mouseexittext": "script", "onfocus": "script", "selectnext": "script", "selectprev": "script",
    "addcolorrange": 6, "align": 1, "appearance_slot": 1, "asset_model": 1, "asset_shader": 1,
    "autowrapped": 0, "backcolor": 4, "background": 1, "border": 1, "bordercolor": 4,
    "bordersize": 1, "cinematic": 1, "columns": "columns", "cvar": 1, "cvarfloat": 4,
    "cvarfloatlist": "list", "cvarstrlist": "list", "cvarsubstring": 0, "cvartest": 1,
    "decoration": 0, "desctext": 1, "disablecvar": "list", "elementheight": 1, "elementtype": 1,
    "elementwidth": 1, "enablecvar": "list", "feeder": 1, "flag": 1, "focussound": 1, "font": 1,
    "forecolor": 4, "group": 1, "hidecvar": "list", "horizontalscroll": 0, "invertyesno": 1,
    "ischaracter": 1, "issaber": 1, "issaber2": 1, "lineheight": 1, "maxchars": 1,
    "maxpaintchars": 1, "model_angle": 1, "model_fovx": 1, "model_fovy": 1, "model_g2anim": 1,
    "model_g2maxs": 3, "model_g2mins": 3, "model_g2skin": 1, "model_origin": 3,
    "model_rotation": 1, "name": 1, "notselectable": 0, "outlinecolor": 4, "ownerdraw": 1,
    "ownerdrawflag": 1, "rect": 4, "scrollhidden": 0, "showcvar": "list", "special": 1,
    "style": 1, "text": 1, "text2": 1, "text2alignx": 1, "text2aligny": 1, "textalign": 1,
    "textalignx": 1, "textaligny": 1, "textscale": 1, "textstyle": 1, "type": 1, "visible": 1,
    "wrapped": 0, "xoffset": 1, "maxlinechars": 1, "rectcvar": 1,
}
MENU_ARGS = {
    "onclose": "script", "onaccept": "script", "onesc": "script", "onopen": "script",
    "appearanceincrement": 1, "backcolor": 4, "background": 1, "border": 1, "bordercolor": 4,
    "bordersize": 1, "cinematic": 1, "descalignment": 1, "desctextstyle": 1, "desccolor": 4,
    "descx": 1, "descy": 1, "descscale": 1, "disablecolor": 4, "fadeclamp": 1, "fadecycle": 1,
    "fadeamount": 1, "focuscolor": 4, "font": 1, "forecolor": 4, "fullscreen": 1, "name": 1,
    "outlinecolor": 4, "outofboundsclick": 0, "ownerdraw": 1, "ownerdrawflag": 1, "popup": 0,
    "rect": 4, "soundloop": 1, "style": 1, "visible": 1, "ignoreescape": 0,
}
COMMANDS = {
    "activate", "close", "exec", "fadein", "fadeout", "hide", "open", "opengotomenu", "orbit",
    "play", "playvoice", "stopvoice", "setasset", "setbackground", "setcolor", "setcvar",
    "setcvartocvar", "setfocus", "setitemcolor", "setitemcolorcvar", "setitemflag",
    "setitemrectcvar", "setplayerhead", "setplayermodel", "setteamcolor", "show", "showmenu",
    "scale", "disable", "transition", "transition2", "setitembackground", "setitemtext",
    "setitemrect", "defer", "rundeferred", "delay", "transition3", "incrementfeeder",
    "decrementfeeder", "uiscript", "ui_script", "playlooped",
}


@dataclass
class Item:
    props: dict = field(default_factory=dict)
    line: int = 0

    def get(self, key, default=None):
        return self.props.get(key, default)


@dataclass
class Menu:
    props: dict = field(default_factory=dict)
    items: list[Item] = field(default_factory=list)
    source: str = ""

    def get(self, key, default=None):
        return self.props.get(key, default)


@dataclass
class MenuFile:
    path: str
    assets: dict = field(default_factory=dict)
    menus: list[Menu] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


class Parser:
    def __init__(self, tokens: list[Token], path: str):
        self.t = tokens
        self.i = 0
        self.path = path
        self.warnings: list[str] = []

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def next(self):
        tok = self.peek()
        self.i += 1
        if tok is None:
            raise SyntaxError(f"{self.path}: unexpected end of file")
        return tok

    def is_punct(self, value):
        tok = self.peek()
        return tok is not None and tok.kind == PUNCT and tok.value == value

    def expect(self, value):
        tok = self.next()
        if tok.value != value:
            raise SyntaxError(f"{self.path}:{tok.line}: expected {value!r}, got {tok.value!r}")

    def block_tokens(self) -> list[Token]:
        self.expect("{")
        depth, out = 1, []
        while True:
            tok = self.next()
            if tok.kind == PUNCT and tok.value == "{":
                depth += 1
            elif tok.kind == PUNCT and tok.value == "}":
                depth -= 1
                if depth == 0:
                    return out
            out.append(tok)

    def script(self) -> list[list[str]]:
        cmds: list[list[str]] = []
        current: list[str] | None = None
        last_line = -1
        for tok in self.block_tokens():
            if tok.kind == PUNCT:
                if tok.value == ";":
                    current = None
                continue
            is_verb = tok.kind == WORD and tok.value.lower() in COMMANDS
            if current is None or (is_verb and tok.line != last_line):
                if not is_verb:
                    self.warnings.append(f"line {tok.line}: unknown script verb {tok.value!r}")
                current = [tok.value.lower() if is_verb else tok.value]
                cmds.append(current)
            else:
                current.append(tok.value)
            last_line = tok.line
        return cmds

    def values(self, spec, keyword):
        if spec == "script":
            return self.script()
        if spec == "list":
            if not self.is_punct("{"):  # "cvarStrList feeder" takes the list from the feeder
                return [self.next().value]
            return [t.value for t in self.block_tokens() if t.kind != PUNCT]
        if spec == "columns":
            count = int(self.next().value)
            return [[self.next().value for _ in range(3)] for _ in range(count)]
        if spec == 0:
            return True
        if spec == 4 and keyword.endswith("color"):  # "forecolor -1" means player color
            first = self.next().value
            if first.startswith("-"):
                return [first]
            return [first] + [self.next().value for _ in range(3)]
        out = [self.next().value for _ in range(spec)]
        return out[0] if spec == 1 else out

    def body(self, table, handler=None):
        props = {}
        self.expect("{")
        while True:
            tok = self.peek()
            if tok is None:
                raise SyntaxError(f"{self.path}: unexpected end of file")
            if self.is_punct("}"):
                self.next()
                return props
            if tok.kind == PUNCT:
                self.next()
                continue
            key = self.next().value.lower()
            if handler and handler(key, tok):
                continue
            if key not in table:
                self.warnings.append(f"line {tok.line}: unknown keyword {key!r}")
                continue
            props[key] = self.values(table[key], key)

    def menu(self, source) -> Menu:
        menu = Menu(source=source)

        def handle(key, tok):
            if key == "itemdef":
                menu.items.append(Item(self.body(ITEM_ARGS), tok.line))
                return True
            return False

        menu.props = self.body(MENU_ARGS, handle)
        return menu

    def asset_globals(self) -> dict:
        assets = {"fonts": [], "props": {}}
        self.expect("{")
        while not self.is_punct("}"):
            key = self.next().value.lower()
            if key in ("smallfont", "mediumfont", "bigfont"):
                assets["fonts"].append((key, self.next().value, self.next().value))
            elif key == "precachesound":
                self.block_tokens()
            elif key in ("focuscolor", "shadowcolor"):
                assets["props"][key] = [self.next().value for _ in range(4)]
            else:
                assets["props"][key] = self.next().value
        self.next()
        return assets

    def file(self) -> MenuFile:
        result = MenuFile(self.path)
        if self.is_punct("{"):  # hud menus have no outer block
            self.next()
        while self.peek() is not None and not self.is_punct("}"):
            tok = self.next()
            key = tok.value.lower()
            if key == "assetglobaldef":
                result.assets = self.asset_globals()
            elif key == "menudef":
                result.menus.append(self.menu(self.path))
            else:
                self.warnings.append(f"line {tok.line}: unknown top-level {tok.value!r}")
        result.warnings = self.warnings
        return result


def parse_text(text: str, name: str) -> MenuFile:
    return Parser(tokenize(text), name).file()
