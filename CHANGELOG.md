# Changelog

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
