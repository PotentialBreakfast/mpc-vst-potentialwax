#!/usr/bin/env python3
"""New version: bump vst/vst.json (1.2.3 = 1203), write the release notes, CHANGELOG entry and README version line.
The Release workflow then builds and publishes it.

    new_version.py patch|minor|major [notes]      notes: one change per line; empty = the commits since the last release

Prints the new version. Exits 1 with a message if there is nothing to release."""
import datetime, glob, json, os, re, subprocess, sys

bump = sys.argv[1] if len(sys.argv) > 1 else "patch"
notes_in = (sys.argv[2] if len(sys.argv) > 2 else os.environ.get("NOTES", "")).strip()

vst = open("vst/vst.json").read()
name = json.loads(vst)["name"]
v = json.loads(vst)["version"]
M, m, p = v // 1000, v // 100 % 10, v % 100
old = "%d.%d.%d" % (M, m, p)
if bump == "major": M, m, p = M + 1, 0, 0
elif bump == "minor": M, m, p = M, m + 1, 0
else: p += 1
if m > 9 or p > 99: sys.exit("version %d.%d.%d doesn't fit vst.json's number (minor 0-9, patch 0-99)" % (M, m, p))
new = "%d.%d.%d" % (M, m, p)
if os.path.exists(".github/release-notes/v%s.md" % new): sys.exit("v%s already has release notes" % new)

# the notes: typed, or the commits since the last release (leaving out housekeeping)
if notes_in:
    bullets = [re.sub(r"^\s*[-*]\s*", "", l).strip() for l in notes_in.splitlines() if l.strip()]
else:
    try:
        last = subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"], text=True).strip()
        rng = last + "..HEAD"
    except subprocess.CalledProcessError:
        rng = "HEAD"
    subj = subprocess.check_output(["git", "log", "--no-merges", "--format=%s", rng], text=True).splitlines()
    skip = re.compile(r"^(tested\.json|README|Release |Merge |\d+\.\d+\.\d+:)", re.I)
    bullets = [s.strip() for s in subj if s.strip() and not skip.match(s.strip())][::-1]
    if not bullets:
        sys.exit("Nothing has changed since the last release. Type what's new in the notes box and run it again.")
bullets = [b[0].upper() + b[1:] if b else b for b in bullets]

open("vst/vst.json", "w").write(re.sub(r'("version"\s*:\s*)\d+', r"\g<1>%d" % (M * 1000 + m * 100 + p), vst, count=1))

r = open("README.md").read()
r = re.sub(r"· v\d+\.\d+\.\d+\*", "· v%s*" % new, r, count=1)
open("README.md", "w").write(r)

month = datetime.date.today().strftime("%Y-%m")
c = open("CHANGELOG.md").read()
entry = "## %s (%s)\n" % (new, month) + "".join("- %s\n" % b for b in bullets) + "\n"
c = c.replace("# Changelog\n\n", "# Changelog\n\n" + entry, 1) if c.startswith("# Changelog\n\n") else "# Changelog\n\n" + entry + c
open("CHANGELOG.md", "w").write(c)

prev = sorted(glob.glob(".github/release-notes/v*.md"), key=lambda f: [int(x) for x in re.findall(r"\d+", os.path.basename(f))])
dl = ""
if prev:
    t = open(prev[-1]).read()
    pv = re.findall(r"\d+\.\d+\.\d+", os.path.basename(prev[-1]))[0]
    if "**Downloads**" in t: dl = t[t.index("**Downloads**"):].replace(pv, new)
os.makedirs(".github/release-notes", exist_ok=True)
open(".github/release-notes/v%s.md" % new, "w").write("**%s %s**\n\n" % (name, new) + "".join("- %s\n" % b for b in bullets) + ("\n" + dl if dl else ""))
print(new)
