# FAQ and troubleshooting

Can't find your answer? [Ask in an issue](../../issues/new/choose). There are no silly questions.

## Installing

**The plugin isn't in my plugin list.**
- **MPC OS 3.10.1+:** check **Preferences → Enhancements (Beta)**: **Enable Enhancements** and **External Plugin Support** must both be ticked, and the plugin's folder must sit directly inside **VST2 Plugins** at the top of the drive.
- **Restart:** the list only updates after the MPC app restarts, so turn the MPC off and on if in doubt.
- **The wrong list:** PotentialWax is an **instrument**: make a plugin track and look in the plugin list.
- **The MPC isn't set up for third-party plugins yet:** see [Before you start](INSTALL-GUIDE.md#before-you-start-is-your-mpc-ready-for-plugins).
- **After a firmware update:** an update can switch plugin loading off again. Redo the set-up from [Open MPC](https://open-mpc.github.io/).

**It's in the list but only shows "Load Plugin" or a plain page of sliders.**
The plugin folder is on a drive the MPC can't run programs from, or isn't in a folder the MPC knows about. Reinstall with the [MPC Plugin Installer](INSTALL-GUIDE.md#way-1-the-easy-way-the-mpc-plugin-installer-app), which picks a suitable place.

**The installer says "No space left on device" or "settings edit failed".**
The MPC's internal storage is full. A common cause is the MPC's own cache of plugin graphics in `/var/tmp/filmstrips`, which can grow to gigabytes. With the MPC app stopped, deleting `/var/tmp/filmstrips/temp_*` is safe, because the MPC makes new ones when it needs them.

**Which file do I download?**
On **MPC OS 3.10.1 or later**: the one ending **`-akai-vst2.zip`**. On older MPC OS 3.x with the community set-up: the one ending **`-mpc-armv7.zip`**. The "Source code" links are for developers.

**Will it work on my MPC One+, Live III or Key 37?**
Yes, on **MPC OS 3.10.1 or later**, with the **`-akai-vst2.zip`**: it has builds for 32-bit and 64-bit MPCs. The 64-bit one hasn't been tried on a real Gen2 MPC yet, so please [tell me how it goes](../../issues/new/choose).

## PotentialWax

**It says no songs were found.**
Make a folder called `MELODIC LIBRARY` at the top of a drive (or any top-level folder with **melodic**, **wax**, **flip** or **songs** in its name), put MP3, WAV or FLAC files in it, then tap **RESCAN** (on the BROWSE page).

**It changes songs by itself.**
Your MIDI input is probably set to **Global**, so the plugin hears MIDI meant for other tracks. Set the MIDI input to the track instead. (From 1.1.1 MIDI can't change songs anyway.)

**The tempo or key is wrong.**
Put the tempo in the file name, for example `Song Name 92bpm.mp3`. For the key, set **KEY** to Original and use **PITCH** by ear.

**Pad 1 doesn't play the first cue.**
Tap **LEARN** (SETUP page), then hit the pad you want as pad 1.

**Where does SAVE PADS put the files?**
In `PotentialWax/<song name>/` on the drive the song is on, as `<song> 01.wav` to `16.wav`.

**Where does SAVE KIT put the kit?**
In `PotentialWax/Kits` on the drive the song is on, unless `PotentialWax/kit-folder.txt` (or PotentialBreaks' `kit-folder.txt`) there names another folder. Load the `.xpm` like any drum program; the `.mid` beside it plays the 16 pads in order.

## Everything else

**Is it free? Can I use it in my music?**
Yes and yes. It's MIT-licensed, so use it however you like, commercially too. Credit is appreciated but not required.

**Can I suggest an idea or report a bug?**
Please do: [ideas](../../issues/new?template=idea.yml) · [bugs](../../issues/new?template=bug.yml).
