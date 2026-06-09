#!/usr/bin/env python3
"""
export_sav.py -- turn captured Pokémon into a Generation 1 .sav file.

The Gen 1 save's party structure (count + species list + 6x44 mon + 6x11 OT +
6x11 nickname = 404 bytes at 0x2F2C) is byte-identical to the party portion of
the trade block we capture. So we drop our party in, set the trainer name and the
save checksum, and write a 32 KB .sav that PKHeX (and emulators) can open.

Usage:
    python host/export_sav.py                         # from vault/party0*.pk1
    python host/export_sav.py a.pk1 b.pk1 ...          # specific Pokémon
    python host/export_sav.py --out mygame.sav

Note: this produces a save whose *party* holds your Pokémon with a valid main
checksum — ideal for opening in PKHeX and dragging them into a real save. Booting
it directly in an emulator works too, but importing via PKHeX is the safe path.
"""
import argparse
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from picovault import engine  # noqa: E402

# US/International Gen 1 save offsets (Bulbapedia / PKHeX SAV1).
SAV_SIZE = 0x8000
OT_NAME_OFF = 0x2598
PARTY_OFF = 0x2F2C
CURRENT_BOX_OFF = 0x30C0
CHECKSUM_OFF = 0x3523
CHECKSUM_START = 0x2598
PARTY_BLOCK_LEN = 404
# Within the trade block: party data begins at 0x0B; OT names at 0x11B.
TB_PARTY_START = 0x0B
TB_OT_START = 0x11B
NAME_LEN = 0x0B


class _FakeLink:
    def send_byte(self, b, n): pass
    def receive_byte(self, n=None): return 0


def build_party_block(trader, party):
    """The 404-byte save party structure for `party` (an RBYTradingData)."""
    from utilities.rby_trading_data_utils import RBYUtils

    sec = party.create_trading_data(trader.special_sections_len)
    sec1, sec2 = list(sec[1]), list(sec[2])
    RBYUtils.apply_patches(sec1, sec2, RBYUtils)  # restore real 0xFE bytes
    block = bytes(sec1[TB_PARTY_START:TB_PARTY_START + PARTY_BLOCK_LEN])
    ot = bytes(sec1[TB_OT_START:TB_OT_START + NAME_LEN])  # first mon's OT name
    return block, ot


def build_sav(party_block, ot_name):
    sav = bytearray(SAV_SIZE)
    sav[PARTY_OFF:PARTY_OFF + len(party_block)] = party_block
    sav[OT_NAME_OFF:OT_NAME_OFF + NAME_LEN] = ot_name
    sav[CURRENT_BOX_OFF] = 0           # empty current box...
    sav[CURRENT_BOX_OFF + 1] = 0xFF    # ...terminated
    s = 0
    for i in range(CHECKSUM_START, CHECKSUM_OFF):
        s = (s + sav[i]) & 0xFF
    sav[CHECKSUM_OFF] = (~s) & 0xFF
    return sav


def party_from_pk1s(trader, files):
    from utilities.rby_trading_data_utils import RBYTradingPokémonInfo

    party = engine.load_base_partner(trader)
    mons = [RBYTradingPokémonInfo.set_data(list(open(f, "rb").read())) for f in files]
    if not 1 <= len(mons) <= 6:
        raise RuntimeError(f"need 1-6 Pokémon, got {len(mons)}")
    party.pokemon = mons
    party.party_info.total = len(mons)
    for i, m in enumerate(mons):
        party.party_info.actual_mons[i] = m.get_species()
    return party


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pk1", nargs="*", help=".pk1 files (default: vault/party0*.pk1)")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault", "pico_ball.sav"))
    args = ap.parse_args()

    files = args.pk1 or sorted(glob.glob(os.path.join(REPO_ROOT, "vault", "party0*.pk1")))
    if not files:
        print("No .pk1 files. Run extract.py first, or pass files.", file=sys.stderr)
        return 2

    with engine.engine_cwd():
        trader = engine.build_trader(_FakeLink(), verbose=False, sanity=True)
        engine.prime_capture_session(trader)
        party = party_from_pk1s(trader, files)
        block, ot = build_party_block(trader, party)
    sav = build_sav(block, ot)

    with open(args.out, "wb") as fh:
        fh.write(sav)
    print(f"Wrote {args.out} ({len(sav)} bytes) with {len(files)} Pokémon in the party.")
    print("Open it in PKHeX (or an emulator) to access them.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
