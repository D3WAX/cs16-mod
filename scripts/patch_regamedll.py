#!/usr/bin/env python3
"""Server side patches for ReGameDLL (the game server used for offline play).

Run from the repository root, after the submodules were checked out:
    python3 scripts/patch_regamedll.py

Adds two multipliers, both read from console variables that the client registers
(see cl_dll/aim_assist.cpp):

  spread_scale  multiplies the bullet spread  (cbase.cpp, CBaseEntity::FireBullets3)
  recoil_scale  multiplies the recoil kick    (player.cpp, CBasePlayer::ItemPostFrame)

They only affect the first human player (client index 1 = the host of a local game)
and never bots. If a cvar does not exist (a plain server), nothing changes.
Running the script twice is safe: files that already have a patch are skipped.
"""
import re
import sys

BASE = "3rdparty/ReGameDLL_CS/regamedll/dlls/"

PATCHES = [
    {
        "name": "spread_scale",
        "path": BASE + "cbase.cpp",
        "marker": "[spread_scale mod]",
        # the line that applies the spread to the shot direction (exactly one in the file)
        "anchor": r"^([ \t]*)vecDir = vecDirShooting \+ x \* vecSpread \* vecRight \+ y \* vecSpread \* vecUp;",
        # "before": insert the block above the anchor line, "replace": swap the anchor line for the block
        "mode": "before",
        "block": [
            "// [spread_scale mod] scale the spread of the local human player (never bots).",
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
        ],
    },
    {
        "name": "recoil_scale",
        "path": BASE + "player.cpp",
        "marker": "[recoil_scale mod]",
        # the call that runs the active weapon (exactly one in the file)
        "anchor": r"^([ \t]*)m_pActiveItem->ItemPostFrame\(\);",
        "mode": "replace",
        "block": [
            "{",
            "\t// [recoil_scale mod] scale the recoil of the local human player (never bots).",
            "\t// Only the kick added by this weapon frame is scaled, the cvar is registered by the client.",
            "\tVector vecPunchBefore = pev->punchangle;",
            "",
            "\tm_pActiveItem->ItemPostFrame();",
            "",
            "\tif ((pev->flags & FL_CLIENT) && !(pev->flags & FL_FAKECLIENT) && ENTINDEX(ENT(pev)) == 1)",
            "\t{",
            "\t\tcvar_t *pRecoilScale = CVAR_GET_POINTER(\"recoil_scale\");",
            "\t\tif (pRecoilScale)",
            "\t\t{",
            "\t\t\tfloat flScale = pRecoilScale->value;",
            "\t\t\tif (flScale < 0.0f) flScale = 0.0f;",
            "\t\t\tif (flScale > 5.0f) flScale = 5.0f;",
            "\t\t\tpev->punchangle = vecPunchBefore + (pev->punchangle - vecPunchBefore) * flScale;",
            "\t\t}",
            "\t}",
            "}",
        ],
    },
]


def apply_patch(p):
    with open(p["path"], "r", newline="") as f:
        text = f.read()

    if p["marker"] in text:
        print("%s: already patched, skipping" % p["name"])
        return True

    anchor = re.compile(p["anchor"], re.MULTILINE)
    matches = list(anchor.finditer(text))
    if len(matches) != 1:
        print("%s: ERROR expected exactly 1 anchor line in %s, found %d" % (p["name"], p["path"], len(matches)))
        return False

    eol = "\r\n" if "\r\n" in text else "\n"
    m = matches[0]
    indent = m.group(1)
    block = "".join(indent + line + eol if line else eol for line in p["block"])

    if p["mode"] == "before":
        text = text[:m.start()] + block + text[m.start():]
    else:
        # swap the anchor line (without its line ending) for the block (without its last line ending)
        text = text[:m.start()] + block.rstrip("\r\n") + text[m.end():]

    with open(p["path"], "w", newline="") as f:
        f.write(text)

    print("%s: patched %s" % (p["name"], p["path"]))
    return True


def main():
    ok = True
    for p in PATCHES:
        ok = apply_patch(p) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
