# Install guide (step by step)

New to adding plugins to an MPC? Start here. It takes about 10 minutes the first time, and after that installing anything is quick.

## On MPC OS 3.10.1 or later: Akai's plugin folder (the easiest way)

Since MPC OS 3.10.1, every standalone MPC (32-bit and 64-bit) can load plugins from other people without any hacking.

1. **Turn plugins on (once).** On the MPC: **Preferences → Enhancements (Beta)**, tick **Enable Enhancements** and **External Plugin Support**.
2. **Download** `PotentialWax-<version>-akai-vst2.zip` from [**Releases**](../../releases) and **unzip** it on your computer. Inside is a folder called **PotentialBreaks - VST - PotentialWax**.
3. **Make the plugin folder (once).** At the very top of a USB stick, SD card or the MPC's internal drive, make a folder called exactly **VST2 Plugins**.
4. **Copy** the whole **PotentialBreaks - VST - PotentialWax** folder into **VST2 Plugins**. Copy the folder, not just the files in it.
5. **Restart** the MPC. It's under **External Plugins**.

Keep the stick in the MPC while you use the plugin. The drive must not be read-only: the LOOK switch saves your choice inside the plugin's folder.

**That's it: you can skip the rest of this guide.** The rest is for older MPC OS 3.x on Gen1 MPCs, using the community set-up. Install one way only, or the plugin shows up twice.

---

## Before you start: is your MPC ready for plugins?

Standalone MPCs don't load plugins from other people out of the box. Your MPC needs to be set up for them once. Pick whichever matches you:

- **I already use other community plugins** (Crate Digger, Lucky Dip, Dexed, anything from [Open MPC](https://open-mpc.github.io/)) → you're ready. Skip to [**Way 1**](#way-1-the-easy-way-the-mpc-plugin-installer-app).
- **I've never added a plugin** → follow the getting-started guide on [**Open MPC**](https://open-mpc.github.io/install.html) first. Its installer app can make a USB stick that sets your MPC up, with no typing, then come back here.

Also check:
- **Which MPC?** This plugin is for the **first-generation** standalones (MPC One, Live, Live II, X). The newer Gen2 models aren't supported yet. See the [compatibility table](README.md#will-it-work-on-my-mpc).
- **MPC OS 3.x.** 2.x isn't supported.
- **Your MPC and computer on the same Wi-Fi or network.**

---

## Way 1 (the easy way): the MPC Plugin Installer app

The free **MPC Plugin Installer** from the Open MPC project does everything from a page on your computer, so there's nothing to type.

1. **Get the installer.** Download it for Windows, Mac or Linux from the [Open MPC website](https://open-mpc.github.io/) ([source and releases](https://github.com/sd88me/mpc-vst-plugins/releases)). It's one file, with nothing to set up.
2. **Get the plugin.** Go to this project's [**Releases**](../../releases) page and download the zip that ends in **`-mpc-armv7.zip`**, for example `PotentialWax-1.1.2-mpc-armv7.zip`. **Don't unzip it.**
3. **Save your project on the MPC.** Installing restarts the MPC app.
4. **Open the installer and Connect** to your MPC (it asks for the MPC's address).
5. **Drop the zip** onto the installer's page, tick it, choose where to put it (a USB drive or the internal drive), and press **Install**.
6. When the MPC has restarted, the plugin is ready. See [Finding it on the MPC](#finding-it-on-the-mpc).

The installer backs up your MPC's settings first, and it can remove the plugin again later.

---

## Way 2: over SSH (for people comfortable with a command line)

You need the MPC's **IP address** (shown in the MPC's network/Wi-Fi settings) and root SSH access.

1. Download the `-mpc-armv7.zip` from [**Releases**](../../releases). Leave it zipped.
2. Copy it to the MPC. On your computer, in a terminal (on Windows use **PowerShell**), from the folder the zip is in, run:
   ```
   scp PotentialWax-1.1.2-mpc-armv7.zip root@<your-mpc-ip>:/tmp/
   ```
3. Log in to the MPC and install. **Save your project first**, because this restarts the MPC app:
   ```
   ssh root@<your-mpc-ip>
   cd /tmp && unzip -o PotentialWax-1.1.2-mpc-armv7.zip
   sh PotentialWax-1.1.2/install.sh -t /media/<your-drive>/Synths
   ```
   `-t` is the `Synths` folder on the drive you keep plugins on, for example a USB stick. Leave it out to use the internal drive.

**Tips**
- **Unzip on the MPC, not on Windows.** Windows unzipping loses permissions the installer needs.
- **Remove it later** with `sh /tmp/PotentialWax-1.1.2/uninstall.sh` (unzip it again first if `/tmp` was cleared).
- **Reference:** `INSTALL.md` inside the zip has all the options.

---

## Finding it on the MPC

It's an **instrument**. Make a **plugin track** and choose **PotentialWax** from the plugin list.

Then tap the plugin's page (the tab at the bottom of the screen) to see its controls. Tap **RANDOM TRACK** (or pick a song on the **BROWSE** page), then play the pads.

Something not right? See the [**FAQ**](FAQ.md).
