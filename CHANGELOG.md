# Changelog

## 1.7.0 (2026-11)
- **MANUAL page.** The plugin's manual is now on the MPC's screen: tap **MANUAL**, then the page buttons along the top (what it does, the controls, how it works with the other PotentialSuite plugins, and good-to-know notes). The full PotentialSuite manual is on the [PotentialSuite page](https://github.com/PotentialBreakfast/potential-plugins).
- One parameter was added at the end of the list (Manual Page); saved projects open as before. Sound and controls are unchanged.

## 1.6.0 (2026-11)
- **FLIP the selected pad**, on the WAVE page: **REVERSE** plays it backwards, **HALF SPEED** plays it at half speed, an octave down, like a record at the wrong speed, and **FILTER** puts a low-pass on it. Mix them as you like; each pad keeps its own, your project remembers them, and SAVE KIT and SAVE PADS save the pads flipped. A new song starts with every pad unflipped.
- **BEAT CHOP** (CUES panel): the 16 pads one beat apart on the song's beat grid, 4 bars from the bar of the last pad you played. Tap it again without playing a pad to move on 4 bars.
- **KEY LOCK** (MATCH panel): one key for everything you dig. Switching it on with KEY on Original takes the key of the song you're on; every song you load then plays in that key, and ROLL leaves the key alone.
- Fixed: with the pitch shifter in use (KEY, PITCH or MATCH MPC with Keep Tempo), a pad stopped about 46 ms before its end. It now plays right up to the next cue.
- Five parameters were added at the end of the list; saved projects open as before.

## 1.5.0 (2026-11)
- **SAVE KIT**: one tap saves the 16 pads as a real **MPC drum program**: each pad as a WAV, as you hear it (at your tempo, key and pitch), on pads A01-A16, with a MIDI file beside it that plays the 16 pads in order on the beat. Load it like any program, and keep it after the plugin is gone. It goes to `PotentialWax/Kits` on the song's drive, or to the folder named on the first line of `PotentialWax/kit-folder.txt` there, or of PotentialBreaks' `kit-folder.txt`, so both plugins can save to the same programs folder. Needs MPC OS 3.10.1 or later (the program format it writes).
- A pad set to **HOLD** is saved as **TO NEXT CUE** (a sample can't know how long you'd hold it). Each saved pad is at most 16 seconds.
- **RESCAN** moved to the BROWSE page, under LIBRARY, to make room for SAVE KIT next to SAVE PADS. One parameter was added at the end of the list (Save Kit); saved projects open as before.

## 1.4.0 (2026-11)
- **ROLL**: one tap for a random song played a random way. **MILD** puts the cues on phrases or bars, in the original key. **MEDIUM** may also cue on the hits, change what a pad plays, move to a new key and nudge the pitch. **WILD** picks any key, can switch to vinyl pitching and moves the pitch up to 5 semitones. TEMPO is left alone, so MATCH MPC keeps it in time. RANDOM TRACK still just picks a song.
- The DIG panel is rearranged to fit ROLL. Two parameters were added at the end of the list (Roll, Roll Wildness); saved projects open as before.

## 1.3.2 (2026-11)
- **CPU figure recorded.** Measured on an MPC One with the Open MPC bench: **PASS**, 5.0% of an audio block at the 99th percentile and 8.0% at worst, so it runs comfortably alongside a full project. It shows on the catalog card and in INSTALL.md.
- Nothing else changed: same sound, controls and saved projects as 1.3.1.

## 1.3.1 (2026-11)
- **Ready for MPC OS 3.10.1's plugin folders.** Every download now carries the `plugin.json` that MPC OS 3.10.1 looks for, so the plugin folder can be copied by hand straight into `VST2 Plugins`.
- **Gen2 MPCs in the Open MPC catalog.** A separate 64-bit download (`-mpc-aarch64.zip`), so the Plugin Manager can install it on Gen2 MPCs, and the catalog's 3.10.1 download (`-mpc-sdk.zip`).
- Built with the current Open MPC kit. Sound, controls and saved projects are unchanged.

## 1.3.0 (2026-11)
- **Three looks.** A new default look, **Print Plate** (a warm printed panel), plus **Night Rack** (dark rack unit) and **Colour Block** (flat, big type), each in PotentialWax's own colour. Pick one with the big LOOK keys on the SETUP page; it shows the next time the plugin loads (take it off and add it again, or load your project again), and it is remembered on your MPC for every project.
- Sound, controls and saved projects are unchanged. One parameter was added at the end of the list (LOOK); it is never saved in a project.
- **Akai's plugin folder format** for MPC OS 3.10.1 and later: a second download (`-akai-vst2.zip`) with 32-bit and 64-bit builds, so it now also runs on Gen2 MPCs. Copy the folder into `VST2 Plugins` and turn on External Plugin Support: no hacking needed.

## 1.2.2 (2026-11)
- Fixed: pressing **Overdub** (or STOP) while the MPC was playing could load a different song. The MPC's host briefly "presses" a plugin's first parameter when it re-prepares audio, and that was RANDOM TRACK. That first slot now does nothing, and RANDOM TRACK is a new parameter at the end. The transport never changes the loaded song or its playback.
- If you had automation or a Q-Link on RANDOM TRACK (unlikely), point it at the new **Random Track** parameter.

## 1.2.1 (2026-11)
- The plugin's name on its page now reads **PotentialWax** (was all capitals), matching its name in the MPC's plugin list. Nothing else changed.

## 1.2.0 (2026-11)
- New **WAVE** page: the waveform with all 16 cue points, the selected pad highlighted and a playhead.
- Zoom (whole song, 16, 4 or 1 bar around the selected cue).
- Move the selected cue by a bar, a beat, a 1/16 or 5 ms; PLAY PAD, < PAD / PAD >, SNAP TO HIT and RESET CUE.

## 1.1.5 (2026-11)
- Cut loops are recognised: exact tempo from the length, bar 1 at the start of the file, cues on the loop's beat grid (a pad per beat on a 4-bar loop). INFO shows "LOOP n BARS".
- Tempo and the beat grid now also listen to the hi-hats, and every grid cue snaps onto the actual hit nearby.
- When a song has too few bar cues, the rest go on beats instead of evenly spaced points.
- New ÷2 / ×2 buttons (MATCH panel) for half-time and double-time samples; saved with the project.
- Files from about 2 seconds long are loaded (was 8), so 1- and 2-bar loops work.

## 1.1.2 (2026-11)
- First public release, in its own repository.
- New look: Barlow fonts, raised panels, glowing knob arcs, even-sized button labels.

## Before the public release
- 1.1.1: MIDI from other tracks can no longer change the song (NRPN control off). Fixed a harmless warning when no song folders exist.
- 1.1.0: BROWSE page: 10 songs at a time, tap to load; PAGE UP / PAGE DOWN; LIBRARY.
- 1.0.0: MP3/WAV/FLAC library, tempo, downbeat and key detection, 16 automatic cues (Phrases / Bars / Hits), Pads and Keys modes, tempo and key matching (Keep Tempo stretch or Vinyl), SAVE PADS.
