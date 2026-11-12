#!/usr/bin/env python3
"""Build a Potential plugin's three looks and merge them into one skin the plugin can switch between.

    build_looks.py <port>/vst <mpc-vst-plugins dir>

1. make_looks.py writes the three layouts from vst/layout.base.conf.
2. The kit's gen_vst.py builds each look as a normal skin (same parameters, same controls).
3. The kit's make_skinset.py merges them: Plugin Skins/print, /rack and /block, with Print Plate active.
The result is the usual vst/build/skin/<vendor> - VST - <name> folder (and vst/build/params.h).
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_looks  # noqa: E402


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    vst, kit = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    cfg = json.load(open(os.path.join(vst, "vst.json")))
    make_looks.make(vst)
    skin = os.path.join(vst, "build", "skin", "%s - VST - %s" % (cfg["vendor"], cfg["name"]))
    looks = os.path.join(vst, "build", "looks")
    shutil.rmtree(looks, ignore_errors=True)
    os.makedirs(looks)
    built = []
    for look in make_looks.LOOKS:
        tmp = os.path.join(vst, "_vst.%s.json" % look)
        json.dump(dict(cfg, layout=make_looks.FILES[look]), open(tmp, "w"))
        try:
            shutil.rmtree(skin, ignore_errors=True)
            subprocess.run([sys.executable, os.path.join(kit, "tools", "gen_vst.py"), tmp], check=True)
        finally:
            os.remove(tmp)
        dst = os.path.join(looks, look)
        shutil.move(os.path.join(skin, "Plugin Skins"), dst)
        built.append("%s=%s" % (look, dst))
    subprocess.run([sys.executable, os.path.join(kit, "tools", "make_skinset.py"), "-o", os.path.join(skin, "Plugin Skins")]
                   + built + ["--active", "print"], check=True)
    print("looks: built %s" % skin)


if __name__ == "__main__":
    main()
