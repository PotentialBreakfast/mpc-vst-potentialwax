#!/usr/bin/env python3
"""The Hi-Fi look's pictures, drawn with numpy + Pillow (no image files to ship): make_looks.py calls write_all().

    _bg_hifi.png        the page: walnut cheeks, a brushed charcoal faceplate, cross-head screws, a pinstripe
    _knob_hifi.png      the knob filmstrip (128 frames): a domed black cap with a cast shadow and a specular
                        highlight, a cream pointer, the value lit in the plugin's colour with a soft glow
    _knob_hifi_hero.png the main knob: the same with a ring in the plugin's colour
    _hifi_defs.svg      gradients and lighting filters hifi.css uses (moulded keys, glass displays, raised panels),
                        put into the renderer's page by the layout's art_defs= line

The knob and the keys follow PotentialBox's Hi-Fi look (potentialbox/skins.py), so the suite matches the groovebox.
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

FRAMES = 128
KNOB_PX = 192          # the strip's frame size; MPC scales it to each knob (box 2r+8)


def rgb(c):
    c = str(c).lstrip("#")
    return np.array([int(c[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32)


def tup(c, a=255):
    return tuple(int(v) for v in rgb(c)) + (a,)


# ---------------------------------------------------------------- knob
def _cap(size, face="24252a", hi="6a6b75", ring="4b4942"):
    """A domed cap, `size` px across, lit from the top left (RGBA)."""
    ss = 3
    n = size * ss
    R = n / 2.0
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    dx, dy = (x + 0.5 - R) / R, (y + 0.5 - R) / R
    d = np.sqrt(dx * dx + dy * dy)
    base, light, rim = rgb(face), rgb(hi), rgb(ring)
    z = np.sqrt(np.clip(1.0 - d * d, 0.0, 1.0))
    lam = np.clip(dx * -0.42 + dy * -0.52 + z * 0.95, 0.0, 1.6)
    radial = np.clip(1.0 - d, 0.0, 1.0) ** 0.85
    col = base + (light - base) * (0.14 + 0.52 * radial)[..., None]
    col *= (0.62 + 0.52 * lam)[..., None]
    sp = np.exp(-(((dx + 0.30) ** 2 + (dy + 0.38) ** 2) / (2 * 0.16 ** 2)))
    col += sp[..., None] * 46.0
    # fine concentric machining on the top
    col += (np.sin(d * 140.0) * 2.2)[..., None] * (d < 0.8)[..., None]
    edge = np.clip((d - 0.78) / 0.22, 0.0, 1.0)
    col *= (1.0 - 0.42 * edge)[..., None]
    col[d > 0.955] = rim
    alpha = np.clip((1.0 - d) * R, 0.0, 1.0) * 255.0
    arr = np.dstack([np.clip(col, 0, 255), alpha]).astype(np.uint8)
    return Image.fromarray(arr, "RGBA").resize((size, size), Image.LANCZOS)


def knob_strip(path, accent, hero=False, frames=FRAMES, px=KNOB_PX):
    """The knob filmstrip, frames stacked down, minimum first. Proportions follow the renderer's knob box (2r+8)."""
    ss = 2
    S = px * ss
    H = S / 2.0
    cap_d = int(S * 0.70)
    arc_r, arc_w = S * 0.445, max(2, int(S * 0.052))
    acc = tup(accent)
    track = tup("34353b")

    # still parts: shadow, cap, ring (hero), track
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    off = int(S * 0.045)
    r0 = cap_d / 2.0
    ImageDraw.Draw(sh).ellipse([H - r0, H - r0 + off, H + r0, H + r0 + off], fill=(0, 0, 0, 170))
    base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(S * 0.03)))
    box = [H - arc_r, H - arc_r, H + arc_r, H + arc_r]
    ImageDraw.Draw(base).arc(box, 135, 405, fill=track, width=arc_w)
    cap = _cap(cap_d)
    base.alpha_composite(cap, (int(H - cap_d / 2), int(H - cap_d / 2)))
    if hero:
        hr = r0 * 1.0
        ImageDraw.Draw(base).ellipse([H - hr, H - hr, H + hr, H + hr], outline=acc, width=max(2, int(S * 0.018)))

    strip = Image.new("RGBA", (px, px * frames), (0, 0, 0, 0))
    for i in range(frames):
        t = i / (frames - 1)
        fr = base.copy()
        if t > 0.004:
            lit = Image.new("RGBA", (S, S), (0, 0, 0, 0))
            ImageDraw.Draw(lit).arc(box, 135, 135 + 270 * t, fill=acc, width=arc_w)
            glow = lit.filter(ImageFilter.GaussianBlur(S * 0.018))
            fr.alpha_composite(glow)
            fr.alpha_composite(glow)
            fr.alpha_composite(lit)
            # a hot core along the lit arc
            core = Image.new("RGBA", (S, S), (0, 0, 0, 0))
            hot = tuple(min(255, int(v * 0.55 + 255 * 0.45)) for v in acc[:3]) + (200,)
            ImageDraw.Draw(core).arc(box, 135, 135 + 270 * t, fill=hot, width=max(1, arc_w // 3))
            fr.alpha_composite(core)
        # pointer: a tapered cream line from the hub towards the edge of the cap, with a soft shadow
        ang = math.radians(-135 + 270 * t)
        sx, sy = math.sin(ang), -math.cos(ang)
        nx, ny = -sy, sx
        r_in, r_out = r0 * 0.18, r0 * 0.80
        w_in, w_out = S * 0.026, S * 0.016
        poly = [(H + sx * r_in + nx * w_in, H + sy * r_in + ny * w_in), (H + sx * r_out + nx * w_out, H + sy * r_out + ny * w_out),
                (H + sx * r_out - nx * w_out, H + sy * r_out - ny * w_out), (H + sx * r_in - nx * w_in, H + sy * r_in - ny * w_in)]
        ps = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(ps).polygon([(x, y + S * 0.008) for x, y in poly], fill=(0, 0, 0, 150))
        fr.alpha_composite(ps.filter(ImageFilter.GaussianBlur(S * 0.006)))
        pd = ImageDraw.Draw(fr)
        pd.polygon(poly, fill=tup("f2ecdc"))
        hub = r0 * 0.12
        pd.ellipse([H - hub, H - hub, H + hub, H + hub], fill=tup("e9e2d0"))
        strip.paste(fr.resize((px, px), Image.LANCZOS), (0, i * px))
    strip.save(path, optimize=True)


# ---------------------------------------------------------------- the page
def _grain(w, h, seed, vertical=True):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    t = x if vertical else y
    u = y if vertical else x
    warp = np.sin(u / 37.0 + 1.1) * 2.4 + np.sin(u / 11.0) * 0.7
    g = (np.sin((t + warp) / 1.6 + 1.3) * 0.40 + np.sin((t + warp) / 4.3 + 4.1) * 0.32 + np.sin((t + warp) / 13.0 + 2.2) * 0.28)
    g += rng.normal(0.0, 0.06, (h, w))
    return g


def background(path, accent, w=1280, h=628, cheek=12):
    """Walnut cheeks left and right, a brushed charcoal faceplate between them, four cross-head screws, a pinstripe."""
    img = np.zeros((h, w, 3), dtype=np.float32)
    # faceplate: a soft top-to-bottom gradient, brushed horizontally, a little noise
    top, bot = rgb("1e1f24"), rgb("121317")
    t = np.linspace(0.0, 1.0, h, dtype=np.float32)[:, None, None]
    plate = top[None, None, :] * (1 - t) + bot[None, None, :] * t
    plate = np.repeat(plate, w, axis=1)
    rng = np.random.default_rng(5)
    streak = rng.normal(0.0, 1.0, (h, 1)).astype(np.float32)
    streak = np.repeat(streak, w, axis=1)
    streak = np.asarray(Image.fromarray(((streak + 4) * 30).clip(0, 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur((18, 0.4))), dtype=np.float32) / 30.0 - 4
    plate += (streak * 1.6 + rng.normal(0.0, 1.3, (h, w)))[..., None]
    # a faint sheen across the upper third
    yy = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    plate += 7.0 * np.exp(-((yy - 0.18) ** 2) / 0.02)
    img[:] = plate
    # walnut cheeks
    for x0 in (0, w - cheek):
        g = _grain(cheek, h, 11 + x0, vertical=True)
        wood = rgb("6d4527")[None, None, :] * np.clip(0.80 + 0.34 * g, 0.5, 1.3)[..., None]
        xs = np.linspace(-1, 1, cheek, dtype=np.float32)[None, :, None]
        wood *= 1.0 - 0.35 * xs ** 2                     # rounded edge: darker at both sides
        wood += 26.0 * np.exp(-((xs + 0.35) ** 2) / 0.08)  # a varnish highlight
        img[:, x0:x0 + cheek] = wood
    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").convert("RGBA")
    d = ImageDraw.Draw(out)
    d.line([(cheek, 0), (cheek, h)], fill=(8, 6, 4, 255), width=1)
    d.line([(w - cheek - 1, 0), (w - cheek - 1, h)], fill=(8, 6, 4, 255), width=1)
    d.rectangle([cheek, h - 4, w - cheek - 1, h - 3], fill=tup(accent))
    # screws, drawn large and scaled down so they are smooth
    for sx, sy in ((cheek + 12, 9), (w - cheek - 12, 9), (cheek + 12, h - 12), (w - cheek - 12, h - 12)):
        k = 8
        s = Image.new("RGBA", (12 * k, 12 * k), (0, 0, 0, 0))
        sd = ImageDraw.Draw(s)
        sd.ellipse([1 * k, 2 * k, 11 * k, 12 * k], fill=(0, 0, 0, 120))
        n = 10 * k
        yy2, xx2 = np.mgrid[0:n, 0:n].astype(np.float32)
        dd = np.sqrt((xx2 - n / 2) ** 2 + (yy2 - n / 2) ** 2) / (n / 2)
        lam = np.clip(1.0 - np.sqrt(((xx2 - n * 0.38) ** 2 + (yy2 - n * 0.32) ** 2)) / (n * 0.8), 0, 1)
        c = 70 + 150 * lam
        head = np.dstack([c, c, c * 1.02, (dd <= 1) * 255]).clip(0, 255).astype(np.uint8)
        s.alpha_composite(Image.fromarray(head, "RGBA"), (k, k))
        sd.line([(3.4 * k, 6 * k), (8.6 * k, 6 * k)], fill=(30, 30, 33, 255), width=int(1.1 * k))
        sd.line([(6 * k, 3.4 * k), (6 * k, 8.6 * k)], fill=(30, 30, 33, 255), width=int(1.1 * k))
        s = s.resize((12, 12), Image.LANCZOS)
        out.alpha_composite(s, (sx - 6, sy - 6))
    out.convert("RGB").save(path, optimize=True)


# ---------------------------------------------------------------- gradients and filters for hifi.css
DEFS = """<svg xmlns="http://www.w3.org/2000/svg"><defs>
<linearGradient id="hf-cream" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#f1ece0"/><stop offset="0.07" stop-color="#e6e0d1"/><stop offset="0.55" stop-color="#d6cfbf"/><stop offset="1" stop-color="#b9b2a2"/>
</linearGradient>
<linearGradient id="hf-amber" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#ffe2a6"/><stop offset="0.07" stop-color="#f7d18b"/><stop offset="0.55" stop-color="#efc271"/><stop offset="1" stop-color="#d29f4c"/>
</linearGradient>
<linearGradient id="hf-dark" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#4a4b52"/><stop offset="0.08" stop-color="#36373d"/><stop offset="0.6" stop-color="#2a2b30"/><stop offset="1" stop-color="#1c1d21"/>
</linearGradient>
<linearGradient id="hf-panel" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#26272d"/><stop offset="1" stop-color="#1b1c21"/>
</linearGradient>
<radialGradient id="hf-glass" cx="0.5" cy="0.45" r="0.75">
 <stop offset="0" stop-color="#13261a"/><stop offset="0.7" stop-color="#0b160f"/><stop offset="1" stop-color="#060b08"/>
</radialGradient>
<linearGradient id="hf-well" x1="0" y1="0" x2="0" y2="1">
 <stop offset="0" stop-color="#08090b"/><stop offset="1" stop-color="#16171b"/>
</linearGradient>
<pattern id="hf-scan" width="4" height="3" patternUnits="userSpaceOnUse"><rect width="4" height="1" fill="#000" fill-opacity="0.22"/></pattern>
<filter id="hf-key" x="-8%" y="-12%" width="116%" height="140%" color-interpolation-filters="sRGB">
 <feGaussianBlur in="SourceAlpha" stdDeviation="1.6" result="b"/>
 <feSpecularLighting in="b" surfaceScale="2.6" specularConstant="0.45" specularExponent="26" lighting-color="#ffffff" result="s">
  <feDistantLight azimuth="245" elevation="52"/></feSpecularLighting>
 <feComposite in="s" in2="SourceAlpha" operator="in" result="si"/>
 <feComposite in="SourceGraphic" in2="si" operator="arithmetic" k1="0" k2="1" k3="0.55" k4="0" result="lit"/>
 <feDropShadow in="lit" dx="0" dy="3" stdDeviation="2.2" flood-color="#000" flood-opacity="0.72"/>
</filter>
<filter id="hf-keylow" x="-6%" y="-10%" width="112%" height="130%" color-interpolation-filters="sRGB">
 <feGaussianBlur in="SourceAlpha" stdDeviation="1.2" result="b"/>
 <feSpecularLighting in="b" surfaceScale="2" specularConstant="0.35" specularExponent="24" lighting-color="#ffffff" result="s">
  <feDistantLight azimuth="245" elevation="55"/></feSpecularLighting>
 <feComposite in="s" in2="SourceAlpha" operator="in" result="si"/>
 <feComposite in="SourceGraphic" in2="si" operator="arithmetic" k1="0" k2="1" k3="0.4" k4="0" result="lit"/>
 <feDropShadow in="lit" dx="0" dy="2" stdDeviation="1.4" flood-color="#000" flood-opacity="0.65"/>
</filter>
<filter id="hf-inset" x="-2%" y="-4%" width="104%" height="108%" color-interpolation-filters="sRGB">
 <feFlood flood-color="#000" flood-opacity="0.85" result="f"/>
 <feComposite in="f" in2="SourceAlpha" operator="out" result="o"/>
 <feOffset in="o" dy="2.5" result="oo"/><feGaussianBlur in="oo" stdDeviation="2.4" result="ob"/>
 <feComposite in="ob" in2="SourceAlpha" operator="in" result="inner"/>
 <feMerge><feMergeNode in="SourceGraphic"/><feMergeNode in="inner"/></feMerge>
</filter>
<filter id="hf-raised" x="-2%" y="-3%" width="104%" height="108%" color-interpolation-filters="sRGB">
 <feTurbulence type="fractalNoise" baseFrequency="0.004 0.8" numOctaves="2" seed="9" result="n"/>
 <feColorMatrix in="n" type="matrix" values="0 0 0 0 1  0 0 0 0 1  0 0 0 0 1  0 0 0 0.035 0" result="brush"/>
 <feComposite in="brush" in2="SourceGraphic" operator="in" result="br"/>
 <feMerge result="m"><feMergeNode in="SourceGraphic"/><feMergeNode in="br"/></feMerge>
 <feDropShadow in="m" dx="0" dy="3" stdDeviation="3.5" flood-color="#000" flood-opacity="0.6"/>
</filter>
<filter id="hf-glow" x="-30%" y="-60%" width="160%" height="220%">
 <feGaussianBlur in="SourceGraphic" stdDeviation="2.2" result="g"/>
 <feMerge><feMergeNode in="g"/><feMergeNode in="SourceGraphic"/></feMerge>
</filter>
</defs></svg>
"""


def write_all(ldir, accent):
    """Every Hi-Fi picture for one plugin into vst/skin/looks/ (files start with "_": made at build time)."""
    knob_strip(os.path.join(ldir, "_knob_hifi.png"), accent)
    knob_strip(os.path.join(ldir, "_knob_hifi_hero.png"), accent, hero=True)
    background(os.path.join(ldir, "_bg_hifi.png"), accent)
    open(os.path.join(ldir, "_hifi_defs.svg"), "w").write(DEFS)


if __name__ == "__main__":
    import sys
    write_all(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "e0803c")
