# Building PotentialWax

You don't need to build it to use it: download the zip from [Releases](../../releases). This is for people who want to change it.

It's built with sd88me's [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) kit, and needs Linux or WSL, `gcc`, Python 3, `pip install ziglang pillow playwright` and `python -m playwright install chromium` (the skin is drawn by the kit's browser renderer). You don't need Docker.

```
git clone https://github.com/sd88me/mpc-vst-plugins
git clone https://github.com/PotentialBreakfast/mpc-vst-potentialwax
cd mpc-vst-plugins && git apply ../mpc-vst-potentialwax/shared/mpc-vst-plugins-label-fit.patch && cd ..
K=$PWD/mpc-vst-plugins; P=$PWD/mpc-vst-potentialwax

# 1. the three looks' skins + params.h (drawn in Chromium from vst/skin/looks/; don't set SHADOW_TITLE_FONT)
mkdir -p $P/vst/build
gcc -O2 -I $K/tools/vendor/force-shadow/tools -o $P/vst/build/shadow_art $K/tools/shadow_art.c -lm
python3 $P/shared/looks/build_looks.py $P/vst $K

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

# 5. Akai's plugin folder format (MPC OS 3.10.1+): a 64-bit build, then Akai's converter and checker
#    (https://github.com/inmusic-software/mpc-plugin-builder-sdk)
python3 -m ziglang cc -target aarch64-linux-gnu.2.31 -mcpu=cortex_a55 -O2 -fPIC -shared -fvisibility=hidden -std=gnu11 \
  -ffast-math -I src -I vst/build -I $K/wrapper src/wax.c $K/wrapper/vst2_wrap.c -lm -lpthread -Wl,--no-undefined -Wl,-s \
  -o vst/build/potentialwax-arm64.so
python3 shared/akai/akai_pack.py --sdk <sdk> --zip dist/PotentialWax-X.Y.Z-mpc-armv7.zip --so64 vst/build/potentialwax-arm64.so \
  --out dist --category "Sampler"
```

**Notes**
- **Parameters are append-only.** `vst/params.lock` keeps saved projects working, so add new parameters at the end of the list.
- **The looks:** every Potential plugin ships three looks and a LOOK selector (parameter `theme`): Print Plate (the default), Night Rack and Colour Block. `vst/layout.base.conf` is the only layout to edit: where each control sits, with no colours. `shared/looks/make_looks.py` turns it into `vst/layout.conf`, `layout.rack.conf` and `layout.block.conf`, adding each look's palette, stylesheet (`vst/skin/looks/print.css`, `rack.css`, `block.css`) and this plugin's own colour (`vst/skin/looks/plugin.json`). `shared/looks/build_looks.py` builds all three and merges them with the kit's `make_skinset.py`.
- **Switching looks:** MPC reads a skin only when a plugin loads, so the plugin copies the chosen look's `TUI.json` and Q-Link maps into place and the new look shows on the next load (`shared/looks/potential_theme.h`; the method is the kit's `docs/SKIN_VARIANTS.md`). `shared/looks/test_theme.c` tests it on the computer.
- **The patch:** `shared/mpc-vst-plugins-label-fit.patch` keeps button labels an even size (they shrink to fit, and only big buttons get bigger text) and makes group headings amber. It's made against mpc-vst-plugins `bf1054c`.
- **Akai's format:** `shared/akai/akai_pack.py` adds the 64-bit binary and `<uiversion>` to what Akai's `legacy_convert.py` makes, keeps both binaries next to `Plugin Skins` (the LOOK switch finds the skins beside the .so), and fails unless Akai's `validate.py` reports no errors and no warnings.
