# Building PotentialWax

You don't need to build it to use it: download the zip from [Releases](../../releases). This is for people who want to change it.

It's built with sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) kit, and needs Linux or WSL, `gcc`, Python 3, `pip install ziglang pillow playwright` and `python -m playwright install chromium` (the skin is drawn by the kit's browser renderer). You don't need Docker.

```
git clone https://github.com/sd88me/mpc-vst-plugins
git clone https://github.com/PotentialBreakfast/mpc-vst-potentialwax
cd mpc-vst-plugins && git apply ../mpc-vst-potentialwax/shared/mpc-vst-plugins-label-fit.patch && cd ..
K=$PWD/mpc-vst-plugins; P=$PWD/mpc-vst-potentialwax

# 1. skin + params.h (drawn in Chromium from vst/skin/; don't set SHADOW_TITLE_FONT)
mkdir -p $P/vst/build
gcc -O2 -I $K/tools/vendor/force-shadow/tools -o $P/vst/build/shadow_art $K/tools/shadow_art.c -lm
python3 $K/tools/gen_vst.py $P/vst/vst.json

# 2. test on your computer (x86, with AddressSanitizer)
bash $K/tools/test_port.sh $P/vst/vst.json

# 3. build for the MPC (Gen1, 32-bit ARM)
cd $P && python3 -m ziglang cc -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a9 -mfpu=neon -O2 -fPIC -shared \
  -fvisibility=hidden -std=gnu11 -ffast-math -I src -I vst/build -I $K/wrapper src/wax.c $K/wrapper/vst2_wrap.c \
  -lm -lpthread -Wl,--no-undefined -Wl,-s -o vst/build/potentialwax.so

# 4. the installer zip
python3 $K/tools/release.py --so vst/build/potentialwax.so --skin "vst/build/skin/PotentialWax - VST - PotentialWax" \
  --entry vst/build/pluginlist-entry.xml --version X.Y.Z --id potentialwax --repo PotentialBreakfast/mpc-vst-potentialwax --license MIT -o dist
python3 $K/tools/catalog_check.py dist/*.zip --catalog
```

**Notes**
- **Parameters are append-only.** `vst/params.lock` keeps saved projects working, so add new parameters at the end of the list.
- **The look:** `vst/skin/` holds the shared Potential skin: `potential-skin.css` (fonts, panel depth, glowing knob arcs), `bg.svg` (the background) and the Barlow fonts. The `theme_*` colours at the top of `vst/layout.conf` come from [`shared/potential-theme.conf`](shared/potential-theme.conf).
- **The patch:** `shared/mpc-vst-plugins-label-fit.patch` keeps button labels an even size (they shrink to fit, and only big buttons get bigger text) and makes group headings amber. It's made against mpc-vst-plugins `bf1054c`.
