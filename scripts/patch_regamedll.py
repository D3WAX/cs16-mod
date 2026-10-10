#!/usr/bin/env python3
"""Adds a "spread_scale" multiplier to ReGameDLL's FireBullets3 (server side).

Run from the repository root, after the submodules were checked out:
    python3 scripts/patch_regamedll.py

The multiplier is read from the console variable "spread_scale", which the client
registers (see cl_dll/aim_assist.cpp). It only affects the first human player
(client index 1 = the host of a local game) and never bots. If the cvar does not
exist, nothing changes. Running the script twice is safe.
"""
import re
import sys

PATH = "3rdparty/ReGameDLL_CS/regamedll/dlls/cbase.cpp"
MARKER = "[spread_scale mod]"

ANCHOR = re.compile(
    r"^([ \t]*)vecDir = vecDirShooting \+ x \* vecSpread \* vecRight \+ y \* vecSpread \* vecUp;",
    re.MULTILINE,
)

BLOCK = [
    "// " + MARKER + " scale the spread of the local human player (never bots).",
    "// The cvar is registered by the client, so on a plain server it does not exist.",
    "if (pevAttacker && (pevAttacker->flags & FL_CLIENT) && !(pevAttacker->flags & FL_FAKECLIENT) && ENTINDEX(ENT(pevAttacker)) == 1)",
    "{",
    "\tcvar_t *pSpreadScale = CVAR_GET_POINTER(\"spread_scale\");",
    "\tif (pSpreadScale)",
    "\t{",
    "\t\tfloat flScale = pSpreadScale->value;",
    "\t\tif (flScale < 0.0f) flScale = 0.0f;",
    "\t\tif (flScale > 5.0f) flScale = 5.0f;",
    "\t\tvecSpread *= flScale;",
    "\t}",
    "}",
    "",
]


def main():
    with open(PATH, "r", newline="") as f:
        text = f.read()

    if MARKER in text:
        print("already patched, nothing to do")
        return 0

    matches = list(ANCHOR.finditer(text))
    if len(matches) != 1:
        print("ERROR: expected exactly 1 anchor line in %s, found %d" % (PATH, len(matches)))
        return 1

    eol = "\r\n" if "\r\n" in text else "\n"
    indent = matches[0].group(1)
    block = "".join(indent + line + eol if line else eol for line in BLOCK)

    start = matches[0].start()
    text = text[:start] + block + text[start:]

    with open(PATH, "w", newline="") as f:
        f.write(text)

    print("patched " + PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
