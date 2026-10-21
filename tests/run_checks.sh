#!/bin/sh
# ============================================================
#  All checks that run without Windows and without 3ds Max:
#   1. plugin sources against the SDK stand-in, Max 2022..2027
#   2. core tests (ASan + UBSan) on synthetic WolvenKit output,
#      including the CLI driver with a fake WolvenKit
#   3. MAXScript files: bracket balance and string termination
#  usage: sh tests/run_checks.sh
# ============================================================
cd "$(dirname "$0")/.." || exit 1
T=$(mktemp -d)
FAIL=0
for R in 24000 25000 26000 27000 28000 29000; do
  Y=$((2022 + (R - 24000) / 1000))
  if g++ -std=c++17 -Wall -Wextra -Wshadow -Werror -DMAX_RELEASE=$R -Itests/sdkstub \
       src/max/*.cpp src/core/*.cpp tests/sdkstub/stub_main.cpp -o "$T/stub" 2>"$T/err.txt"; then
    echo "  plugin vs SDK stand-in, Max $Y: ok"
  else
    echo "  plugin vs SDK stand-in, Max $Y: FAILED"; head -20 "$T/err.txt"; FAIL=1
  fi
done
python3 tests/make_fixture.py "$T/fixture" >/dev/null || exit 1
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wshadow \
    src/core/*.cpp tests/test_core.cpp -o "$T/test_core" || exit 1
chmod +x tests/fake_wolvenkit.py
CP2077IMPORT_HOME="$T/home" FAKE_WK_STORE="$T/fixture" "$T/test_core" "$T/fixture" "$PWD/tests/fake_wolvenkit.py" "$T/work" | tail -3 || FAIL=1
python3 - scripts/*.mcr scripts/*.ms <<'PY' || FAIL=1
import sys, re
bad = 0
for p in sys.argv[1:]:
    s = open(p, encoding="utf-8").read()
    depth = {"(": 0, "[": 0}
    i = 0; line = 1; instr = False
    while i < len(s):
        c = s[i]
        if c == "\n": line += 1
        if instr:
            if c == "\\": i += 2; continue
            if c == '"': instr = False
        elif c == '"': instr = True
        elif s.startswith("--", i):
            while i < len(s) and s[i] != "\n": i += 1
            continue
        elif c in "([": depth[c] += 1
        elif c == ")": depth["("] -= 1
        elif c == "]": depth["["] -= 1
        if depth["("] < 0 or depth["["] < 0:
            print("  %s:%d unbalanced" % (p, line)); bad = 1; break
        i += 1
    if instr: print("  %s: unterminated string" % p); bad = 1
    elif depth["("] or depth["["]: print("  %s: %d '(' and %d '[' left open" % (p, depth["("], depth["["])); bad = 1
    else: print("  %s: brackets ok" % p)
sys.exit(bad)
PY
rm -rf "$T"
[ "$FAIL" = "0" ] && echo "ALL CHECKS PASSED" || { echo "CHECKS FAILED"; exit 1; }
