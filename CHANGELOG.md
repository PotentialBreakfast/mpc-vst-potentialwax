# Changelog

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
