#!/usr/bin/env python3
"""Mark tested: add a tested.json entry (version, device, firmware, date) and, with a bench result, vst/bench.txt
(the CPU figure the next release shows).

    mark_tested.py <version> <device> <firmware> [bench JSON line]"""
import datetime, json, os, sys

ver, dev, fw = sys.argv[1].lstrip("v"), sys.argv[2], sys.argv[3]
bench = sys.argv[4].strip() if len(sys.argv) > 4 else ""
today = datetime.date.today().isoformat()
name = json.load(open("vst/vst.json"))["name"]
changed = []

t = json.load(open("tested.json")) if os.path.exists("tested.json") else []
if not any(e.get("version") == ver and e.get("device") == dev and e.get("firmware") == fw for e in t):
    t.append({"version": ver, "device": dev, "firmware": fw, "date": today})
    open("tested.json", "w").write(json.dumps(t, indent=2) + "\n")
    changed.append("tested.json")

if bench:
    b = json.loads(bench)   # must be the bench's own JSON line
    for k in ("p99_pct", "max_pct", "verdict"):
        if k not in b: sys.exit("the bench line has no %s" % k)
    b["plugin"] = name
    text = "# MPC TOOLS bench (Open MPC tools/bench.c -j) on an %s, %s, MPC running, %s, %s %s\n%s\n" % (
        dev, fw, today, name, ver, json.dumps(b))
    old = open("vst/bench.txt").read() if os.path.exists("vst/bench.txt") else ""
    if old.split("\n", 1)[-1].strip() != json.dumps(b):
        open("vst/bench.txt", "w").write(text)
        changed.append("vst/bench.txt")
print(" ".join(changed) if changed else "nothing new")
