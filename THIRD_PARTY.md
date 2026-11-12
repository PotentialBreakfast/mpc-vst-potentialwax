# Third-party code

The released PotentialWax zips include code from these projects, under their own licences.

## mpc-vst-plugins (sd88me) – MIT

The plugin wrapper (`wrapper/vst2_wrap.c`), skin builder and installer scripts come from https://github.com/sd88me/mpc-vst-plugins. They are compiled into or shipped with every release zip. `shared/mpc-vst-plugins-label-fit.patch` is a small change to that kit.

```
MIT License

Copyright (c) 2026 sd88me

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Third-party files in this repository keep their own licences and are not
relicensed by this file: tools/vendor/ (each folder has its own LICENSE),
tools/html_art/fonts/ (SIL Open Font License, see OFL.txt), and any file whose
header names another licence. Plugins listed in the catalog are separate
projects with their own licences.
```

## dr_libs (David Reid) – public domain (Unlicense) or MIT-0

`src/vendor/dr_wav.h`, `dr_flac.h` and `dr_mp3.h`, from https://github.com/mackron/dr_libs. Details and the exact version are in `src/vendor/VENDORED.md`.

## Fonts – SIL Open Font License 1.1

Used to draw the three looks (the text is baked into the skin's pictures; the font files themselves are only in this repository, in `vst/skin/looks/fonts/`, each with its licence `OFL-*.txt`): **Jost** (The Jost Project Authors), **DM Mono** (The DM Mono Project Authors), **IBM Plex Sans Condensed** and **IBM Plex Mono** (IBM Corp.), **Bricolage Grotesque** (The Bricolage Grotesque Project Authors) and **Space Mono** (The Space Mono Project Authors).

## Fonts – SIL Open Font License 1.1

**Barlow** and **Barlow Condensed** by Jeremy Tribby (https://github.com/jpt/barlow), from Google Fonts. The `.ttf` files and the licence (`OFL-Barlow.txt`) are in `vst/skin/fonts/`. They are used to draw the plugin's page; the fonts themselves are not changed.
