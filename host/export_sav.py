#!/usr/bin/env python3
"""
export_sav.py -- turn captured Pokémon into a Generation 1 or 2 .sav file.

Gen 1: the save's party structure (count + species list + 6x44 mon + 6x11 OT +
6x11 nickname = 404 bytes at 0x2F2C) is byte-identical to the party portion of
the trade block we capture. We drop our party in, set the trainer name and the
save checksum, and write a 32 KB .sav that PKHeX (and emulators) can open.

Gen 2 (Gold/Silver/Crystal): the GSC party list (1 count + 7 species/terminator +
6x48 mon + 6x11 OT + 6x11 nick = 428 bytes) is also the same structure the trade
sends. GSC stores two copies of the checksummed block, each with a 16-bit sum;
PKHeX validates both stored sums against the primary region, so we write the party
list, the trainer name, both checksum words, and the backup copy. Offsets come
from PKHeX SAV2Offsets (Crystal vs G/S differ). Default game is Crystal.

Usage:
    python host/export_sav.py                         # Gen 1, from vault/party0*.pk1
    python host/export_sav.py a.pk1 b.pk1 ...          # specific Pokémon
    python host/export_sav.py --gen 2                  # Gen 2 (Crystal layout)
    python host/export_sav.py --gen 2 --game gs        # Gen 2 (Gold/Silver layout)
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

from picovault import engine, savedata  # noqa: E402

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

# --- Gen 2 (GSC) ------------------------------------------------------------
# Save layout per game, from PKHeX SAV2Offsets (US/International). The checksum
# is a 16-bit little-endian sum of every byte in [ck_start, ck_end] (inclusive),
# stored at BOTH ck1 and ck2. `backups` mirror the primary block so the real game
# (not just PKHeX) accepts the save; each entry is (src, length, dst).
GSC_MON_LEN = 0x30      # 48-byte party struct (held item at byte 0x01)
GSC_PARTY_BLOCK_LEN = 1 + 7 + 6 * GSC_MON_LEN + 6 * NAME_LEN + 6 * NAME_LEN  # 428
GSC_GAMES = {
    "c": {
        "ot": 0x200B, "party": 0x2865, "box": 0x2D10,
        "ck_start": 0x2009, "ck_end": 0x2B82, "ck1": 0x2D0D, "ck2": 0x1F0D,
        "backups": [(0x2009, 0xB7A, 0x1209)],
    },
    "gs": {
        "ot": 0x200B, "party": 0x288A, "box": 0x2D6C,
        "ck_start": 0x2009, "ck_end": 0x2D68, "ck1": 0x2D69, "ck2": 0x7E6D,
        "backups": [
            (0x2009, 0x222F - 0x2009, 0x15C7),
            (0x222F, 0x23D9 - 0x222F, 0x3D69),
            (0x23D9, 0x2856 - 0x23D9, 0x0C6B),
            (0x2856, 0x288A - 0x2856, 0x7E39),
            (0x288A, 0x2D69 - 0x288A, 0x10E8),
        ],
    },
}


def build_party_block_gsc(mons):
    """The 428-byte GSC save party list for a list of GSCTradingPokémonInfo."""
    count = len(mons)
    block = bytearray(GSC_PARTY_BLOCK_LEN)
    block[0] = count
    for i, m in enumerate(mons):
        block[1 + i] = m.get_species()
    block[1 + count] = 0xFF                      # species-list terminator
    mon_base = 8
    ot_base = mon_base + 6 * GSC_MON_LEN         # 0x128
    nick_base = ot_base + 6 * NAME_LEN           # 0x16A
    for i, m in enumerate(mons):
        block[mon_base + i * GSC_MON_LEN:mon_base + i * GSC_MON_LEN + GSC_MON_LEN] = bytes(m.values)
        block[ot_base + i * NAME_LEN:ot_base + i * NAME_LEN + NAME_LEN] = bytes(m.ot_name.values)
        block[nick_base + i * NAME_LEN:nick_base + i * NAME_LEN + NAME_LEN] = bytes(m.nickname.values)
    return bytes(block), bytes(mons[0].ot_name.values)


def build_sav_gsc(party_block, ot_name, game="c"):
    g = GSC_GAMES[game]
    sav = bytearray(SAV_SIZE)
    sav[g["party"]:g["party"] + len(party_block)] = party_block
    sav[g["ot"]:g["ot"] + NAME_LEN] = ot_name
    # Empty current box (count 0 + 0xFF terminator): PKHeX detects a Gen 2 save by
    # validating BOTH the party AND current-box lists, so the box can't be blank.
    sav[g["box"]] = 0
    sav[g["box"] + 1] = 0xFF
    s = sum(sav[g["ck_start"]:g["ck_end"] + 1]) & 0xFFFF   # 16-bit byte sum
    for pos in (g["ck1"], g["ck2"]):
        sav[pos] = s & 0xFF
        sav[pos + 1] = (s >> 8) & 0xFF
    for src, length, dst in g["backups"]:                 # mirror to backup block
        sav[dst:dst + length] = sav[src:src + length]
    return sav


def party_from_pk2s(trader, files):
    from utilities.gsc_trading_data_utils import GSCTradingPokémonInfo

    mons = [GSCTradingPokémonInfo.set_data(list(open(f, "rb").read())) for f in files]
    if not 1 <= len(mons) <= 6:
        raise RuntimeError(f"need 1-6 Pokémon, got {len(mons)}")
    return mons


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
    ap.add_argument("pk1", nargs="*",
                    help="record files (default: vault/party0*.pk1 for Gen 1, "
                         ".pk2/.pk1 for Gen 2)")
    ap.add_argument("--gen", type=int, choices=(1, 2), default=1,
                    help="game generation: 1 = R/B/Y, 2 = G/S/C")
    ap.add_argument("--game", choices=("c", "gs"), default="c",
                    help="Gen 2 save layout: c = Crystal (default), gs = Gold/Silver")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault", "pico_ball.sav"))
    args = ap.parse_args()

    if args.pk1:
        files = args.pk1
    else:
        vault = os.path.join(REPO_ROOT, "vault")
        pats = ("party0*.pk2", "party0*.pk1") if args.gen == 2 else ("party0*.pk1",)
        files = sorted({p for pat in pats for p in glob.glob(os.path.join(vault, pat))})
    if not files:
        print("No record files. Run extract.py first, or pass files.", file=sys.stderr)
        return 2

    # Warn (don't block) if a file's apparent gen doesn't match --gen: a .sav is
    # gen-specific, so a Gen 1 record won't build a valid Gen 2 party and vice versa.
    for f in files:
        rgen = savedata.infer_gen(f)
        if rgen and rgen != args.gen:
            print(f"Warning: {os.path.basename(f)} looks like a Gen {rgen} record "
                  f"but --gen {args.gen} was given; the .sav may be wrong.",
                  file=sys.stderr)

    with engine.engine_cwd():
        if args.gen == 2:
            trader = engine.build_trader(_FakeLink(), verbose=False, sanity=True, gen=2)
            engine.prime_capture_session(trader)
            mons = party_from_pk2s(trader, files)
            block, ot = build_party_block_gsc(mons)
            sav = build_sav_gsc(block, ot, game=args.game)
        else:
            trader = engine.build_trader(_FakeLink(), verbose=False, sanity=True)
            engine.prime_capture_session(trader)
            party = party_from_pk1s(trader, files)
            block, ot = build_party_block(trader, party)
            sav = build_sav(block, ot)

    with open(args.out, "wb") as fh:
        fh.write(sav)
    label = f"Gen 2 {'Crystal' if args.game == 'c' else 'Gold/Silver'}" if args.gen == 2 else "Gen 1"
    print(f"Wrote {args.out} ({len(sav)} bytes) with {len(files)} Pokémon "
          f"in the party ({label}).")
    print("Open it in PKHeX (or an emulator) to access them.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
