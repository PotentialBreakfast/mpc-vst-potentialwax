#!/usr/bin/env python3
"""akai_pack.py: turns a Potential plugin's community release zip (<Name>-<ver>-mpc-armv7.zip) plus its 64-bit build into
the official Akai plugin folder format for MPC OS 3.10.1+ (<Name>-<ver>-akai-vst2.zip), using Akai's own converter and
checker from https://github.com/inmusic-software/mpc-plugin-builder-sdk.

    akai_pack.py --sdk <sdk dir> --zip dist/<Name>-<ver>-mpc-armv7.zip --so64 vst/build/<id>-arm64.so --out dist [--category Effect]

What it adds to Akai's legacy_convert.py output: the 64-bit binary ("file64"), <uiversion>0.1.0.0</uiversion> in
version.xml (the skins use the scripted UI) and an INSTALL.txt. Both binaries stay next to "Plugin Skins" (not in VST/),
because the LOOK switch finds the skins beside the .so. Fails if Akai's validate.py reports any error or warning."""
import argparse, json, os, re, shutil, subprocess, sys, tempfile, zipfile

ap = argparse.ArgumentParser()
ap.add_argument("--sdk", required=True)
ap.add_argument("--zip", required=True)
ap.add_argument("--so64", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--category")
a = ap.parse_args()

work = tempfile.mkdtemp()
conv = os.path.join(work, "conv")
r = subprocess.run([sys.executable, "-I", os.path.join(a.sdk, "tools", "legacy_convert.py"), a.zip, "--out", conv],
                   capture_output=True, text=True)
print(r.stdout.strip())
if r.returncode:
    sys.exit("legacy_convert failed:\n" + r.stderr)
folders = [d for d in os.listdir(conv) if os.path.isdir(os.path.join(conv, d))]
assert len(folders) == 1, folders
name = folders[0]
d = os.path.join(conv, name)

pj = os.path.join(d, "plugin.json")
m = json.load(open(pj))
so64 = os.path.basename(a.so64)
shutil.copy2(a.so64, os.path.join(d, so64))
m["file64"] = so64
m.pop("convertedFrom", None)
if a.category:
    m["category"] = a.category
order = ["name", "manufacturer", "version", "format", "kind", "uid", "category", "numInputs", "numOutputs", "file32", "file64",
         "license", "url"]
m = {**{k: m[k] for k in order if k in m}, **{k: v for k, v in m.items() if k not in order}}
open(pj, "w").write(json.dumps(m, indent=2) + "\n")

vx = os.path.join(d, "version.xml")
t = open(vx).read()
if "<uiversion>" not in t:
    t = re.sub(r"(<version>[^<]*</version>)", r"\1\n\t<uiversion>0.1.0.0</uiversion>", t, count=1)
open(vx, "w").write(t)

r = subprocess.run([sys.executable, "-I", os.path.join(a.sdk, "tools", "validate.py"), d], capture_output=True, text=True)
last = (r.stdout.strip().splitlines() or [""])[-1]
print("validate: " + last)
if r.returncode or "0 errors, 0 warnings" not in last:
    sys.exit("Akai's validate.py is not happy:\n" + r.stdout + r.stderr)

plug, ver, kind = m["name"], m["version"], m.get("kind", "instrument")
where = "an effect: add it as an insert effect" if kind == "effect" else "an instrument: add it as a plugin program"
install = f"""{plug} {ver} - official Akai plugin format (VST2 Plugins folder)

Needs MPC OS 3.10.1 or newer. Works on 32-bit MPCs (One, One+, Live, Live II, X, Key 61, Key 37, Force)
and 64-bit MPCs (Live III, XL and later).

1. On the MPC: Preferences > Enhancements (Beta) > turn on "Enable Enhancements" and "External Plugin Support".
2. On a USB stick, SD card or the MPC's internal drive, make a folder at the very top called exactly:
       VST2 Plugins
3. Copy the folder "{name}" (inside this zip) into "VST2 Plugins".
   Copy the whole folder, not just the files inside it.
4. Restart the MPC. {plug} is under External Plugins: {where}.

LOOKS: Print Plate (default), Night Rack and Colour Block. Pick one on the plugin's LOOK keys;
the new look shows the next time the plugin is loaded. The drive must not be read-only.

Using the community plugin set-up (Open MPC) instead? Use the "-mpc-armv7.zip" download, not this one.
Install it only one way, or {plug} will show up twice.

Free and open source (MIT) by PotentialBreaks.
"""
os.makedirs(a.out, exist_ok=True)
out = os.path.join(a.out, f"{plug}-{ver}-akai-vst2.zip")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(d):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, conv))
    z.writestr("INSTALL.txt", install)
shutil.rmtree(work)
print("akai: " + out)
