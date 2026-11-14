#!/usr/bin/env python3
"""PotentialSuite looks: one layout, three looks.

    make_looks.py <port>/vst        writes vst/layout.conf (Print Plate, the default), vst/layout.rack.conf (Night Rack)
                                    and vst/layout.block.conf (Colour Block) from vst/layout.base.conf, plus each look's
                                    stylesheet and background in vst/skin/looks/ (files starting with "_").

vst/layout.base.conf is the only layout edited by hand: where every control sits, with no colours. Each look adds its
own palette (theme_* lines), stylesheet and background, and recolours the few colours a layout line names itself.
vst/skin/looks/plugin.json gives the plugin its own colour in each look and names its main ("hero") knob or knobs:

    {"accent": {"print": "1f8f80", "rack": "3ccfb8", "block": "38ae9c"}, "hero": "magnitude"}

("wave": "skin/wave/strip.png" also redraws that waveform filmstrip in each look; `picture` lines get their SVGs recoloured.)

vst/manual.txt, when there is one, becomes a MANUAL tab: pages of text with a row of page buttons (parameter
"manual_page", one option per page; see manual_pages()). shared/potential_manual.h holds the page in the engine.

A SETTINGS tab with the LOOK selector (parameter "theme") is added to every look. The three skins are then built and
merged by build_looks.py; the plugin switches between them by itself (shared/potential_theme.h).
"""
import json
import os
import re
import sys

LOOKS = ("print", "rack", "block")
TITLES = {"print": "Print Plate", "rack": "Night Rack", "block": "Colour Block"}
FILES = {"print": "layout.conf", "rack": "layout.rack.conf", "block": "layout.block.conf"}


def mix(a, b, t):
    """colour a moved t of the way to colour b"""
    return "%02x%02x%02x" % tuple(round(int(a[i:i + 2], 16) * (1 - t) + int(b[i:i + 2], 16) * t) for i in (0, 2, 4))


def palette(look, a, lit):
    """theme_* lines of a look for a plugin whose colour in that look is `a` (`lit`: its colour on a dark display), and
    the colours its own lines and pictures use: title (headline text), dim (quiet captions), disp (live text in a
    display window), key / primary (a button's own fill), cool (a second caption colour), m_* (meter pictures),
    w_* (the waveform strip)."""
    if look == "print":
        t = dict(bg="d9d3c7", panel="d9d3c7", line="1b1a18", ink="1b1a18", ink_dim="3d3a34", ink_faint="8a8374",
                 accent=a, accent_hi="1b1a18", knob_face="1b1a18", knob_ring="1b1a18", knob_dot="efe8d8",
                 seg_active="1b1a18", seg_inactive="e7e1d5", seg_active_tx="efe8d8", btn_text="1b1a18",
                 btn_bg="e7e1d5", btn_on="aaa290", tile_on="3a3f3c", lcd="111514", box="dfd9cd")
        own = dict(title="1b1a18", dim="3d3a34", disp="efe8d8", key="e7e1d5", plate="d9d3c7", cool="3d3a34", primary=a,
                   m_bg="111514", m_off="262b29", m_on=lit, m_hi=mix(lit, "ffffff", 0.4), m_cool="a9b5af", m_mid="efe8d8",
                   w_bar="8f9a95", w_cue=lit, w_selbg="2a302e", w_sel="efe8d8", glow=False)
    elif look == "rack":
        t = dict(bg="121826", panel="121826", line="4a5674", ink="ece5d3", ink_dim="aeb7cc", ink_faint="6a7694",
                 accent=a, accent_hi="ece5d3", knob_face="1e2740", knob_ring="26304a", knob_dot=a,
                 seg_active="ece5d3", seg_inactive="161d2f", seg_active_tx="121826", btn_text="ece5d3",
                 btn_bg="1e2740", btn_on="3a4662", tile_on="26304a", lcd="080c16", box="151c2d")
        own = dict(title="ece5d3", dim="aeb7cc", disp=a, key="1e2740", plate="121826", cool="8fb4d8", primary="3a4662",
                   m_bg="080c16", m_off="1c2438", m_on=a, m_hi=mix(a, "ffffff", 0.4), m_cool="aeb7cc", m_mid="ece5d3",
                   w_bar="8a94ad", w_cue=a, w_selbg="1c2438", w_sel="ece5d3", glow=True)
    else:
        t = dict(bg=a, panel=a, line="0e1f1b", ink="0e1f1b", ink_dim="0e1f1b", ink_faint="2c403a",
                 accent="f2ecdd", accent_hi="0e1f1b", knob_face="0e1f1b", knob_ring="0e1f1b", knob_dot="f2ecdd",
                 seg_active="0e1f1b", seg_inactive=a, seg_active_tx="f2ecdd", btn_text="0e1f1b",
                 btn_bg=a, btn_on=mix(a, "0e1f1b", 0.3), tile_on="2c403a", lcd="0e1f1b", box=a)
        own = dict(title="0e1f1b", dim="0e1f1b", disp="f2ecdd", key=a, plate=a, cool="0e1f1b", primary="f2ecdd",
                   m_bg="0e1f1b", m_off="223530", m_on="f2ecdd", m_hi="ffffff", m_cool=a, m_mid="f2ecdd",
                   w_bar="5f7a72", w_cue=a, w_selbg="2c403a", w_sel="f2ecdd", glow=False)
    return t, own


# colours the old dark skin's layout lines named, and what they meant
OLD = {"f2b34a": "title", "f5ab3d": "title", "dc9b3c": "title", "e6e9ee": "title", "ffd680": "title", "8b929c": "dim",
       "5b616b": "dim", "2a2e34": "key", "24282d": "key", "0c0e10": "lcd", "a86f1a": "primary", "8fb4d8": "cool"}
# the colours the meter pictures (vst/meter/*.svg) were drawn in
METER = {"0a0c0f": "m_bg", "1d2127": "m_off", "211d14": "m_off", "2d2614": "m_off", "2a1715": "m_off", "f2b34a": "m_on",
         "f5c15a": "m_hi", "8fb4d8": "m_cool", "e6e9ee": "m_mid"}


def recolour_svg(src, own):
    """A meter picture in this look's colours (warning reds stay red); flat looks lose the glow."""
    src = re.sub(r'((?:fill|flood-color)="#)([0-9a-fA-F]{6})(")',
                 lambda m: m.group(1) + (own[METER[m.group(2).lower()]] if m.group(2).lower() in METER else m.group(2)) + m.group(3), src)
    if not own["glow"]:
        src = re.sub(r'\sfilter="url\(#[a-z]\)"', "", src)
    return src


def wave_strip(own, path):
    """The waveform column pictures (18 x 200, 36 of them stacked): 9 bar heights x plain / cue / selected cue / playing now."""
    from PIL import Image, ImageDraw
    k, w, h = 4, 18, 200
    rgb = lambda c: tuple(int(c[i:i + 2], 16) for i in (0, 2, 4)) + (255,)
    strip = Image.new("RGBA", (w, h * 36))
    for mark in range(4):
        for lvl in range(9):
            im = Image.new("RGBA", (w * k, h * k), rgb(own["w_selbg"] if mark == 2 else own["m_bg"]))
            d = ImageDraw.Draw(im)
            bar = rgb(own["w_sel"] if mark == 2 else own["w_bar"])
            if lvl == 0:
                d.rectangle([3 * k, 100 * k - 2, 15 * k, 100 * k + 1], fill=rgb(own["m_off"]))
            else:
                half = max(5, lvl * 95 // 8)
                d.rounded_rectangle([3 * k, (100 - half) * k, 15 * k - 1, (100 + half) * k], radius=3 * k, fill=bar)
            if mark in (1, 2):
                line = rgb(own["w_sel"] if mark == 2 else own["w_cue"])
                d.rectangle([0, 0, 3 * k - 1, h * k], fill=line)
                d.polygon([(0, 0), (11 * k, 0), (0, 11 * k)], fill=line)
            elif mark == 3:
                d.rectangle([7 * k, 0, 10 * k - 1, h * k], fill=(255, 255, 255, 255))
            strip.paste(im.resize((w, h), Image.LANCZOS), (0, (mark * 9 + lvl) * h))
    strip.save(path)


def background(look, own, a):
    """The plate behind everything (1280 x 628): print marks in the corners, rack screws, or the plain colour field."""
    if look == "print":
        marks = "".join(
            '<path d="M%d %d h%d M%d %d v%d" stroke="#1b1a18" stroke-width="1.5" fill="none"/>' % (x, y, dx, x, y, dy)
            for x, y, dx, dy in ((4, 4, 7, 7), (1276, 4, -7, 7), (4, 624, 7, -7), (1276, 624, -7, -7)))
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 628"><rect width="1280" height="628" '
                'fill="#%s"/>%s</svg>' % (own["plate"], marks))
    if look == "rack":
        screws = "".join('<circle cx="%d" cy="%d" r="3.2" fill="none" stroke="#6a7694" stroke-width="1.2"/>' % (x, y)
                         for x, y in ((7, 7), (1273, 7), (7, 621), (1273, 621)))
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 628"><rect width="1280" height="628" '
                'fill="#%s"/>%s</svg>' % (own["plate"], screws))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 628"><rect width="1280" height="628" '
            'fill="#%s"/></svg>' % own["plate"])


def hero_css(look, r):
    """The main knob (the only one of radius r) wears the plugin's colour."""
    f = '.knob-face[r="%d"]' % (r - 2)
    s = '.knob-sheen[r="%d"]' % (r - 2)
    if look == "print":
        return ("%s { fill: var(--accent); stroke: var(--p-ink); stroke-width: 3; }\n"
                "%s { fill: #fff; opacity: 0.16; }\n" % (f, s))
    if look == "rack":
        return "%s { stroke: var(--accent); stroke-width: 3; }\n" % f
    return ("%s { fill: var(--b-cream); stroke: var(--b-ink); stroke-width: 6; }\n"
            "%s ~ .knob-pointer { stroke: var(--b-ink); }\n" % (f, f))


def attr(line, k):
    m = re.search(r'(?:^|\s)%s=("([^"]*)"|\S+)' % re.escape(k), line)
    return None if not m else (m.group(2) if m.group(2) is not None else m.group(1))


def look_block(own):
    """The LOOK selector as a panel in the lower half of an existing SETUP tab."""
    return [
        'frame x=14 y=410 w=1252 h=294 title="LOOK"',
        'enum_h cx=640 cy=540 label="" key=theme sw=372 sh=110',
        'text cx=640 cy=636 label="A NEW LOOK SHOWS THE NEXT TIME THIS PLUGIN LOADS" size=1.9 weight=700 color=%s' % own["title"],
        'text cx=640 cy=674 label="TAKE THE PLUGIN OFF AND ADD IT AGAIN, OR LOAD YOUR PROJECT AGAIN" size=1.6 weight=600 color=%s' % own["dim"],
    ]


def settings_tab(look, own, bgfile):
    big = {"print": 2.6, "rack": 2.6, "block": 3.0}[look]
    return [
        "[tab SETTINGS]",
        "art file=%s" % bgfile,
        'frame x=14 y=100 w=1252 h=604 title="SETTINGS"',
        'text cx=640 cy=214 label="LOOK" size=%g weight=700 color=%s' % (big, own["title"]),
        'enum_h cx=640 cy=340 label="" key=theme sw=372 sh=132',
        'text cx=640 cy=486 label="A NEW LOOK SHOWS THE NEXT TIME THIS PLUGIN LOADS" size=1.9 weight=700 color=%s' % own["title"],
        'text cx=640 cy=536 label="TAKE THE PLUGIN OFF AND ADD IT AGAIN, OR LOAD YOUR PROJECT AGAIN" size=1.6 weight=600 color=%s' % own["dim"],
        'text cx=640 cy=650 label="PotentialSuite  ·  PotentialBreaks" size=1.4 weight=600 case=none color=%s' % own["dim"],
        'qlinks "SETTINGS" = -',
    ]


MANUAL_WRAP = 112   # characters of body text that fit a manual line
MANUAL_LINES = 13   # lines that fit a manual page under its heading


def manual_pages(path):
    """vst/manual.txt -> [(button label, [(kind, text)])]. A line "= LABEL" starts a page (LABEL is its button, and
    one option of the parameter "manual_page", in order); "# text" is a heading; "- text" a bullet; an empty line a
    gap; anything else body text. Long lines are wrapped. A page that does not fit is an error (shorten it)."""
    pages = []
    for raw in open(path, encoding="utf-8"):
        l = raw.rstrip()
        if l.startswith("= "):
            pages.append((l[2:].strip(), []))
            continue
        if not pages:
            if l.strip():
                raise SystemExit("manual.txt: text before the first '= PAGE' line")
            continue
        rows = pages[-1][1]
        if l.startswith("# "):
            rows.append(("head", l[2:].strip()))
        elif not l.strip():
            if rows and rows[-1][0] != "gap":
                rows.append(("gap", ""))
        else:
            bullet = l.startswith("- ")
            words, line, first = (l[2:] if bullet else l).split(), "", True
            width = MANUAL_WRAP - 3 if bullet else MANUAL_WRAP
            for w in words + [None]:
                if w is None or len(line) + len(w) + 1 > width:
                    if line:
                        rows.append(("bullet" if bullet and first else "more" if bullet else "body", line))
                        first = False
                    line = w or ""
                else:
                    line = (line + " " + w).strip()
    for name, rows in pages:
        while rows and rows[-1][0] == "gap":
            rows.pop()
        if '"' in "".join(t for _, t in rows):
            raise SystemExit('manual.txt: page %s: a double quote cannot be drawn (use single quotes)' % name)
        used = sum(0.5 if k == "gap" else 1.35 if k == "head" else 1 for k, _ in rows)
        if used > MANUAL_LINES + 1.35:
            raise SystemExit("manual.txt: page %s is %.1f lines too long" % (name, used - MANUAL_LINES - 1.35))
    return pages


def manual_tab(look, own, bgfile, pages):
    """The MANUAL tab: a row of page buttons (parameter "manual_page") and each page's text, shown for its option."""
    n = len(pages)
    sw = min(190, (1180 - 8 * (n - 1)) // n)
    out = ["[tab MANUAL]", "art file=%s" % bgfile,
           'frame x=14 y=100 w=1252 h=604 title="MANUAL"',
           'enum_h cx=640 cy=166 label="" key=manual_page sw=%d sh=56 rows=1' % sw]
    for i, (name, rows) in enumerate(pages):
        y = 236.0
        for kind, text in rows:
            if kind == "gap":
                y += 16
                continue
            if kind == "head":
                out.append('text cx=60 cy=%d label="%s" size=1.9 weight=700 align=left color=%s when=manual_page:%d'
                           % (y + 4, text.upper(), own["title"], i))
                y += 44
                continue
            x = 60 if kind == "body" else 84
            if kind == "bullet":
                out.append('text cx=62 cy=%d label="-" size=1.45 weight=700 case=none align=left color=%s when=manual_page:%d'
                           % (y, own["title"], i))
            out.append('text cx=%d cy=%d label="%s" size=1.45 weight=600 case=none align=left color=%s when=manual_page:%d'
                       % (x, y, text, own["dim"], i))
            y += 32
    out.append('qlinks "MANUAL" = -')
    return out


def make(vst):
    base = os.path.join(vst, "layout.base.conf")
    ldir = os.path.join(vst, "skin", "looks")
    cfg = json.load(open(os.path.join(ldir, "plugin.json")))
    src = [l.rstrip("\n") for l in open(base)]
    # the main knob(s) wear the plugin's colour. A knob's filmstrip is per radius, so they need a radius no other knob has.
    heroes = cfg.get("hero") or []
    heroes = [heroes] if isinstance(heroes, str) else heroes
    knobs = {attr(l, "key"): int(attr(l, "r")) for l in src if l.startswith("knob ") and attr(l, "r")}
    the_r = None
    if heroes:
        the_r = knobs[heroes[0]]
        others = set(r for k, r in knobs.items() if k not in heroes)
        while the_r in others:
            the_r += 1
    for look in LOOKS:
        a = cfg["accent"][look].lower()
        theme, own = palette(look, a, cfg["accent"]["rack"].lower())
        own["lcd"] = theme["lcd"]
        bgfile = "skin/looks/_bg_%s.svg" % look
        out = ["# %s: the %s look. Made by shared/looks/make_looks.py from layout.base.conf: edit that file, not this one."
               % (cfg.get("name", "Potential plugin"), TITLES[look])]
        out += ["theme_%s=%s" % kv for kv in theme.items()]
        out += ["art_css=skin/looks/_%s.css" % look]
        hero_r = the_r
        made = set()
        tab, placed = None, False
        for l in src:
            s = l.strip()
            if not s or s.startswith("#") or s.startswith("theme_") or s.startswith("art_css="):
                continue
            if s.startswith("[tab "):
                if tab == "SETUP" and not placed:
                    out += look_block(own)
                    placed = True
                tab = s[5:-1].strip()
                out += [s, "art file=%s" % bgfile]
                continue
            # a colour a line names itself: swap the old dark skin's for this look's
            def recolour(m):
                role = OLD.get(m.group(2).lower())
                return m.group(1) + (own[role] if role else m.group(2))
            s = re.sub(r"(\b(?:color|tcolor|ink|ink_dim)=)([0-9a-fA-F]{6})\b", recolour, s)
            kind = s.split(" ", 1)[0]
            if kind == "picture":                 # meter pictures: this look's copies
                files = []
                for f in attr(s, "files").split(","):
                    f = f.strip()
                    if f.endswith(".svg"):
                        g = "skin/looks/_pic_%s/%s" % (look, f.replace("/", "_"))
                        os.makedirs(os.path.dirname(os.path.join(vst, g)), exist_ok=True)
                        open(os.path.join(vst, g), "w").write(recolour_svg(open(os.path.join(vst, f)).read(), own))
                        f = g
                    files.append(f)
                s = re.sub(r'files="[^"]*"', 'files="%s"' % ",".join(files), s)
            if kind == "meter" and cfg.get("wave") and attr(s, "strip") == cfg["wave"]:
                g = "skin/looks/_wave_%s.png" % look
                if g not in made:
                    wave_strip(own, os.path.join(vst, g))
                    made.add(g)
                s = s.replace("strip=%s" % cfg["wave"], "strip=%s" % g)
            if kind in ("readout", "stepper", "list", "popup") and "tcolor=" not in s:
                s += " tcolor=%s" % own["disp"]
            if kind == "knob" and attr(s, "key") in heroes:
                s = re.sub(r"\br=\d+", "r=%d" % hero_r, s, count=1)
            out.append(s)
        if tab == "SETUP" and not placed:
            out += look_block(own)
            placed = True
        manual = os.path.join(vst, "manual.txt")
        if os.path.exists(manual):
            out += manual_tab(look, own, bgfile, manual_pages(manual))
        if not placed:
            out += settings_tab(look, own, bgfile)
        open(os.path.join(vst, FILES[look]), "w").write("\n".join(out) + "\n")
        css = open(os.path.join(ldir, "%s.css" % look)).read()
        css += ("\n/* the LOOK selector's big keys */\n"
                '.seg-bg[height="132"] ~ .seg-tx, .seg-bg[height="110"] ~ .seg-tx { font-size: 27px; letter-spacing: 0.04em; }\n')
        if hero_r:
            css += "\n/* this plugin's main knob */\n" + hero_css(look, hero_r)
        for extra in ("plugin.css", "plugin.%s.css" % look):    # a plugin's own additions: to every look, to this one
            if os.path.exists(os.path.join(ldir, extra)):
                css += "\n" + open(os.path.join(ldir, extra)).read()
        open(os.path.join(ldir, "_%s.css" % look), "w").write(css)
        open(os.path.join(ldir, "_bg_%s.svg" % look), "w").write(background(look, own, a))
    return [FILES[k] for k in LOOKS]


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    print("looks: " + ", ".join(make(os.path.abspath(sys.argv[1]))))
