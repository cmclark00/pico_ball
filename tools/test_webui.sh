#!/usr/bin/env bash
# Validate webui/decode.js against the real Path A vault capture:
#   1. rebuild a 625-byte trade record from vault/party0*.pk1 (via the engine)
#   2. decode it with the actual browser code (webui/decode.js) under Node
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VAULT="$ROOT/vault" python3 - <<'PY'
import sys, os, glob
sys.path.insert(0, "host")
from picovault import engine
class FakeLink:
    def send_byte(self, b, n): pass
    def receive_byte(self, n=None): return 0
vault = os.environ["VAULT"]
files = sorted(glob.glob(os.path.join(vault, "party0*.pk1")))
if not files:
    sys.exit("No vault/party0*.pk1 to test with (run extract.py first).")
with engine.engine_cwd():
    from utilities.rby_trading_data_utils import RBYTradingPokémonInfo
    t = engine.build_trader(FakeLink(), verbose=False, sanity=True)
    engine.prime_capture_session(t)
    party = engine.load_base_partner(t)
    mons = [RBYTradingPokémonInfo.set_data(list(open(f, 'rb').read())) for f in files]
    party.pokemon = mons
    party.party_info.total = len(mons)
    for i, m in enumerate(mons):
        party.party_info.actual_mons[i] = m.get_species()
    sec = party.create_trading_data(t.special_sections_len)
    rec = bytes(sec[0]) + bytes(sec[1]) + bytes(sec[2])
    open("/tmp/rec.hex", "w").write(rec.hex())
print("built 625-byte record from:", [os.path.basename(f) for f in files])
PY

node tools/test_decode.js /tmp/rec.hex "Pikachu:7,Pidgey:3,Squirtle:13,Metapod:6" "$ROOT/vault"
