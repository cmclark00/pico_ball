#!/usr/bin/env bash
# Validate the Gen 1 (.sav) export, including the full-party edge case:
#   1. build synthetic .pk1 records from the engine's RBY base party
#   2. export_sav's party_from_pk1s/build_party_block/build_sav produce the save
#   3. a FULL party of 6 must work (regression: the species-list terminator
#      used to be written past actual_mons and raised IndexError)
#   4. the species list must be [count, species..., 0xFF] with stale base
#      species zeroed, and the main checksum must be valid (PKHeX-style check)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PYTHONPATH=host python3 - <<'PY'
import os, sys, tempfile
sys.path.insert(0, "host")
from picovault import engine
import export_sav as E

class FakeLink:
    def send_byte(self, b, n): pass
    def receive_byte(self, n=None): return 0

def build(trader, files):
    party = E.party_from_pk1s(trader, files)
    block, ot = E.build_party_block(trader, party)
    return bytes(E.build_sav(block, ot)), block

d = tempfile.mkdtemp()
with engine.engine_cwd():
    from utilities.rby_trading_data_utils import RBYTradingPokémonInfo
    trader = engine.build_trader(FakeLink(), verbose=False, sanity=True)
    engine.prime_capture_session(trader)
    base = engine.load_base_partner(trader)
    m = base.pokemon[0]
    template = bytes(m.get_data())  # struct + OT + nickname (66 B)

    # Six distinct records: patch the species byte (Gen 1 internal ids).
    species = [0xB0, 0x99, 0xB1, 0x7C, 0x54, 0x15]  # Charmander..., all valid
    files = []
    for i, sp in enumerate(species):
        rec = bytearray(template); rec[0] = sp
        p = os.path.join(d, f"m{i}.pk1"); open(p, "wb").write(bytes(rec))
        files.append(p)

    # Full party of 6 (the old code crashed here with IndexError).
    sav6, block6 = build(trader, files)
    head6 = list(block6[:8])
    assert head6[0] == 6, head6
    assert head6[1:7] == species, (head6, species)
    assert head6[7] == 0xFF, f"terminator missing for a full party: {head6}"

    # Partial party of 3: terminator right after, stale base species zeroed.
    sav3, block3 = build(trader, files[:3])
    head3 = list(block3[:8])
    assert head3[0] == 3 and head3[1:4] == species[:3], head3
    assert head3[4] == 0xFF, f"terminator misplaced: {head3}"
    assert head3[5:7] == [0, 0], f"stale species not cleared: {head3}"

for name, sav in (("full-6", sav6), ("partial-3", sav3)):
    s = 0
    for i in range(E.CHECKSUM_START, E.CHECKSUM_OFF):
        s = (s + sav[i]) & 0xFF
    assert sav[E.CHECKSUM_OFF] == (~s) & 0xFF, f"{name}: bad main checksum"
    # PKHeX-style Gen 1 detection: party and current-box lists must be valid.
    def list_valid(buf, off, maxc=20):
        count = buf[off]
        return count <= maxc and buf[off + 1 + count] == 0xFF
    assert list_valid(sav, E.PARTY_OFF), f"{name}: party list invalid"
    assert list_valid(sav, E.CURRENT_BOX_OFF), f"{name}: current-box list invalid"

print(f"  full party of 6 exports (terminator 0xFF after 6 species)")
print(f"  partial party of 3: terminator + zeroed tail correct")
print(f"  main checksum + PKHeX party/box list detection OK ({len(sav6)}B)")
print("PASS")
PY
