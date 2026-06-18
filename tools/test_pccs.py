#!/usr/bin/env python3
"""Regression test for the Gen 1/2 -> Gen 3 conversion (host/picovault/pccs.py).

Uses PCCS's own bundled real test mons (Charmander from Gen 1, Cyndaquil from
Gen 2), wrapped into our on-disk record layout (struct + OT + nickname), and
checks the converted .pk3 decodes to the right species with a valid checksum.

Skips (exit 0) if pccs_convert isn't built — run scripts/setup.sh first.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from picovault import pccs  # noqa: E402

# Box structs (and names) lifted verbatim from third_party/PCCS tests/main.cpp.
CHARMANDER_BOX = [0xB0, 0x00, 0x16, 0x06, 0x00, 0x14, 0x14, 0x2D, 0x0A, 0x2D, 0x00,
                  0x00, 0x6F, 0xC6, 0x00, 0x00, 0xCD, 0x00, 0x2C, 0x00, 0x30, 0x00,
                  0x41, 0x00, 0x2B, 0x00, 0x32, 0x31, 0xD9, 0x23, 0x28, 0x00, 0x00]
CYNDAQUIL_BOX = [0x9B, 0x00, 0x21, 0x2B, 0x6C, 0x00, 0x1D, 0x29, 0x00, 0x00, 0xCD,
                 0x00, 0x32, 0x00, 0x41, 0x00, 0x40, 0x00, 0x2B, 0x00, 0x2C, 0x59,
                 0x7B, 0x21, 0x1E, 0x14, 0x00, 0x4C, 0x00, 0x85, 0x01, 0x06]
NICK = [0x80, 0x81, 0x50, 0, 0, 0, 0, 0, 0, 0, 0]
OT = [0x80, 0x81, 0x01, 0x02, 0x50, 0, 0, 0, 0, 0, 0]

_SUBSTRUCT_ORDER = ["GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME",
                    "AEGM", "AEMG", "AMGE", "AMEG", "EGAM", "EGMA", "EAGM", "EAMG",
                    "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"]


def _disk_record(gen, box):
    """Wrap a PCCS box struct into our record layout: struct + OT + nickname.

    convert_record reads OT/nick after the full party struct (44/48 bytes), so we
    pad the box up to that length (the extra party-only bytes are ignored)."""
    struct_len = pccs._STRUCT_LEN[gen]
    pad = [0] * (struct_len - len(box))
    return bytes(box + pad + OT + NICK)


def _species_and_checksum_ok(pk3):
    pid, otid = struct.unpack_from("<II", pk3, 0)
    stored = struct.unpack_from("<H", pk3, 28)[0]
    block = bytearray(pk3[32:80])
    key = pid ^ otid
    for i in range(0, 48, 4):
        v = (int.from_bytes(block[i:i + 4], "little") ^ key) & 0xFFFFFFFF
        block[i:i + 4] = v.to_bytes(4, "little")
    order = _SUBSTRUCT_ORDER[pid % 24]
    g = order.index("G")
    species = int.from_bytes(block[g * 12:g * 12 + 2], "little")
    recomputed = sum(int.from_bytes(block[i:i + 2], "little")
                     for i in range(0, 48, 2)) & 0xFFFF
    return species, (recomputed == stored)


def main():
    if not pccs.available():
        print("SKIP: pccs_convert not built (run scripts/setup.sh). "
              f"Looked for {pccs.PCCS_BIN}")
        return 0

    cases = [(1, CHARMANDER_BOX, 4, "Charmander"),
             (2, CYNDAQUIL_BOX, 155, "Cyndaquil")]
    ok = True
    for gen, box, want_species, name in cases:
        pk3 = pccs.convert_record(gen, _disk_record(gen, box))
        if pk3 is None:
            print(f"FAIL Gen {gen} {name}: converter rejected the mon")
            ok = False
            continue
        species, chk_ok = _species_and_checksum_ok(pk3)
        good = species == want_species and chk_ok and len(pk3) == 80
        print(f"{'PASS' if good else 'FAIL'} Gen {gen} {name}: "
              f"pk3 species={species} (want {want_species}), "
              f"len={len(pk3)}, checksum_ok={chk_ok}")
        ok = ok and good
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
