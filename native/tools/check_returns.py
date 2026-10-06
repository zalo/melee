#!/usr/bin/env python3
"""Lists game functions that can fall off the end without returning a value.

The decomp matches code where the caller picks up whatever the callee left in
r3 or f1, so the native build compiles with -Wno-return-type. A native compiler
returns garbage there. Sites whose callers ignore the value are listed in
return_type_allowlist.txt; anything else this prints needs a native return
(see the "#ifdef MELEE_NATIVE ... return" blocks in src/). Run it after every
upstream merge:

    python3 native/tools/check_returns.py [build dir with compile_commands.json]
"""
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ALLOW = os.path.join(ROOT, "native", "tools", "return_type_allowlist.txt")
FLAGS = ["-fsyntax-only", "-ferror-limit=0", "-fno-caret-diagnostics", "-Wno-everything",
         "-Wreturn-type", "-Wno-error=return-type"]
SILENCERS = ("-Wno-return-type", "-Wno-return-mismatch")


def diagnostics(entry):
    args = shlex.split(entry["command"])
    out = args.index("-o")
    del args[out:out + 2]
    args = [a for a in args if a != "-c" and a not in SILENCERS] + FLAGS
    done = subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True, errors="replace")
    return done.stderr


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "native")
    with open(os.path.join(build, "compile_commands.json")) as f:
        entries = [e for e in json.load(f) if "melee_game.dir" in e["command"]]
    with open(ALLOW) as f:
        allowed = {line.split("#")[0].strip() for line in f} - {""}
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        text = "\n".join(pool.map(diagnostics, entries))
    found = {}
    for line in text.splitlines():
        m = re.match(r"(.+?):(\d+):\d+: warning: non-void function does not return a value", line)
        if m:
            path = m.group(1) if os.path.isabs(m.group(1)) else os.path.join(build, m.group(1))
            where = f"{os.path.relpath(path, ROOT)}:{m.group(2)}"
            found[name_at(path, int(m.group(2))) or where] = where
    new = sorted((site, name) for name, site in found.items() if name not in allowed)
    stale = sorted(allowed - set(found))
    for site, name in new:
        print(f"{site}: {name} can fall off the end and is not in the allow-list")
    for name in stale:
        print(f"note: {name} is in the allow-list but no longer reported")
    print(f"Checked {len(entries)} source files; {len(found)} missing returns, {len(new)} not allowed")
    return 1 if new else 0


HEADER = re.compile(r"^[A-Za-z_][\w\s\*]*?\b(\w+)\s*\(")


def name_at(path, line):
    """The function whose closing brace is on `line`: the definition header above its opening brace."""
    try:
        with open(path, errors="replace") as f:
            lines = f.read().splitlines()[:line]
    except OSError:
        return None
    for i in range(len(lines) - 1, -1, -1):
        if lines[i].startswith("{"):
            for j in range(i, max(i - 12, -1), -1):
                m = HEADER.match(lines[j])
                if m:
                    return m.group(1)
            return None
    return None


if __name__ == "__main__":
    sys.exit(main())
