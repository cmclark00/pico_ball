#!/usr/bin/env bash
# Validate the Gen 2 (.sav) export end to end:
#   1. build a Gen 2 trade record from the engine's GSC base party
#   2. Python export_sav (build_sav_gsc, Crystal) builds a .sav from per-mon .pk2
#   3. the browser code (decode.js buildSav -> buildSavFromMons) builds one from
#      the full record, and the two must be byte-identical
#   4. both stored checksums must equal the primary-region sum (PKHeX-valid)
#   5. PKHeX's actual Gen 2 detection passes (party AND current-box lists valid),
#      ported from SaveUtil.IsListValidG12 / IsG2CrystalINT
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PYTHONPATH=host python3 - <<'PY'
import os, sys, subprocess, tempfile
sys.path.insert(0, "host")
from picovault import engine
import export_sav as E

class FakeLink:
    def send_byte(self, b, n): pass
    def receive_byte(self, n=None): return 0

d = tempfile.mkdtemp()
with engine.engine_cwd():
    trader = engine.build_trader(FakeLink(), verbose=False, sanity=True, gen=2)
    engine.prime_capture_session(trader)
    party = engine.load_base_partner(trader)
    n = party.party_info.get_total()
    party.pokemon[0].values[1] = 0x8f  # Metal Coat: prove the held item lands in the save
    sec = party.create_trading_data(trader.special_sections_len)
    rec = b"".join(bytes(s) for s in sec)
    hexf = os.path.join(d, "rec.hex"); open(hexf, "w").write(rec.hex())

    from utilities.gsc_trading_data_utils import GSCTradingPokémonInfo
    pk2s = []
    for i in range(n):
        m = party.pokemon[i]
        r = bytes(m.values) + bytes(m.ot_name.values) + bytes(m.nickname.values)
        p = os.path.join(d, f"m{i}.pk2"); open(p, "wb").write(r); pk2s.append(p)
    mons = [GSCTradingPokémonInfo.set_data(list(open(p, "rb").read())) for p in pk2s]
    block, ot = E.build_party_block_gsc(mons)
    sav_py = bytes(E.build_sav_gsc(block, ot, game="c"))

out_js = os.path.join(d, "out.sav")
subprocess.run(["node", "tools/build_sav.js", hexf, out_js], check=True)
sav_js = open(out_js, "rb").read()

g = E.GSC_GAMES["c"]
s = sum(sav_py[g["ck_start"]:g["ck_end"] + 1]) & 0xFFFF
ck1 = sav_py[g["ck1"]] | (sav_py[g["ck1"] + 1] << 8)
ck2 = sav_py[g["ck2"]] | (sav_py[g["ck2"] + 1] << 8)

# PKHeX detection (SaveUtil.IsG2CrystalINT -> HasListAt(party 0x2865, box 0x2D10)).
def list_valid(buf, off, maxc=20):
    count = buf[off]
    return count <= maxc and buf[off + 1 + count] == 0xFF
detected = list_valid(sav_py, g["party"]) and list_valid(sav_py, g["box"])

ok = True
if sav_py != sav_js:
    ok = False; print("FAIL: Python and JS .sav differ")
if not (ck1 == s == ck2):
    ok = False; print(f"FAIL: checksum mismatch sum={s:#06x} ck1={ck1:#06x} ck2={ck2:#06x}")
if sav_py[g["party"] + 8 + 1] != 0x8f:
    ok = False; print("FAIL: held item not preserved into the save")
if not detected:
    ok = False; print("FAIL: PKHeX Gen 2 detection would reject (party/box list invalid)")
if not ok:
    sys.exit(1)
print(f"  Python == JS .sav (byte-identical, {len(sav_py)}B)")
print(f"  Crystal checksums valid ({ck1:#06x}); held item preserved")
print("  PKHeX detection OK (party + current-box lists valid)")
print("PASS")
PY
