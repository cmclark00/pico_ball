#!/usr/bin/env bash
# Offline validation of the Gen 3 host path (no hardware):
#   1. fabricate a valid Gen 3 mon through the engine's plaintext constructor
#   2. record round-trip: get_data -> set_data byte-exact (149B)
#   3. capture simulation: build a 0x380 section, party_reader it back,
#      savedata writes .pk3 (100B, PKHeX party format) + lossless JSON
#   4. inject handshake vs a scripted GBA: commit, decline and cancel paths
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PYTHONPATH=host python3 - <<'PY'
import sys, os, json, tempfile
from types import SimpleNamespace
sys.path.insert(0, "host")
from picovault import engine, savedata

def le16(v): return [v & 0xFF, (v >> 8) & 0xFF]
def le32(v): return [(v >> (8*i)) & 0xFF for i in range(4)]

class FakeLink:
    def send_byte(self, b, n): pass
    def receive_byte(self, n=None): return 0

CTRL = 0xA0 << 24  # done_control_flag | in_party_trading_flag

class ScriptedLink:
    """Plays the GBA side of the gen 3 trade menu."""
    def __init__(self, phases):
        self.queue = []
        for v in phases:
            self.queue += [CTRL | v] * 22
        self.sent = []
    def send_byte(self, b, n):
        assert n == 4, f"gen3 must use 4-byte transfers, got {n}"
        self.sent.append(int(b))
    def receive_byte(self, n=None):
        return self.queue.pop(0) if self.queue else CTRL

with engine.engine_cwd():
    from utilities.rse_sp_trading_data_utils import RSESPTradingPokémonInfo as Mon

    # Building a trader initializes RSESPUtils' class-level tables
    # (enc_positions etc.), which the Mon constructor needs.
    trader = engine.build_trader(FakeLink(), verbose=False, sanity=True, gen=3)
    engine.prime_capture_session(trader)

    # 1. fabricate a valid mon (Pikachu Lv5, met-location=trade)
    pid, otid = 0x12345678, 0xDEADBEEF
    v = [0]*100
    v[0:4] = le32(pid); v[4:8] = le32(otid)
    v[8:16] = [0xCA,0xC3,0xC5,0xBB,0xBD,0xC2,0xCF,0xFF]   # "PIKACHU"
    v[18] = 2
    v[20:25] = [0xBD,0xC9,0xCC,0xBF,0xD3]; v[25] = 0xFF    # "COREY"
    growth  = le16(25) + le16(0) + le32(135) + [0,70] + [0,0]
    attacks = le16(84) + le16(45) + le16(0) + le16(0) + [30,40,0,0]
    misc    = [0, 0xFE] + le16(0) + le32(0x15151515) + le32(0)
    plain = growth + attacks + [0]*12 + misc
    v[32:80] = plain
    ck = 0
    for i in range(12):
        w = int.from_bytes(bytes(plain[i*4:(i+1)*4]), "little")
        ck = (ck + w) & 0xFFFF; ck = (ck + (w >> 16)) & 0xFFFF
    v[28:30] = le16(ck)
    v[84] = 5; v[86:88] = le16(20); v[88:90] = le16(20)
    mon = Mon(v, 0, is_encrypted=False)
    mon.add_mail([0]*Mon.mail_len, 0)
    assert mon.is_valid, "fabricated mon must be valid"

    # 2. record round-trip
    rec = bytes(mon.get_data())
    assert len(rec) == 149, len(rec)
    assert bytes(Mon.set_data(list(rec)).get_data()) == rec, "set_data round-trip"
    print("  record round-trip byte-exact (149B)")

    # 3. capture simulation -> savedata
    inj = engine.build_inject_party(trader, rec)
    secs = inj.create_trading_data(trader.special_sections_len)
    assert [len(s) for s in secs] == [896]
    cart = trader.party_reader(secs[0])
    assert bytes(cart.pokemon[0].get_data()) == rec, "section round-trip"
    vault = tempfile.mkdtemp()
    written = savedata.save_party(cart, vault, engine.ENGINE_DIR, gen=3)
    stem, info = written[0]
    pk3 = open(os.path.join(vault, stem + ".pk3"), "rb").read()
    assert pk3 == rec[:100], ".pk3 must be the 100B party struct"
    assert bytes.fromhex(info["raw_record_hex"]) == rec, "JSON lossless record"
    assert info["species_name"] == "Pikachu" and info["ot_name"] == "COREY"
    print(f"  capture sim: {stem}.pk3 (100B PKHeX party) + lossless JSON")

    # padded 100B .pk3 also injects
    inj2 = engine.build_inject_party(trader, rec[:100])
    assert inj2.pokemon[0].get_species() == 25
    print("  bare 100B .pk3 accepted for inject (mail/version/ribbon padded)")

    # 4. inject handshake: commit / decline / cancel
    accepts = [0xA2 << 16, 0xB2 << 16]
    succ = [s << 16 for s in (0x90,0x91,0x92,0x93,0x94,0x95,0x9C)]
    link = ScriptedLink([(0x82 << 16) | 25] + accepts + succ)
    t = engine.build_trader(link, verbose=False, sanity=True, gen=3)
    t.sleep_timer = 0
    engine.prime_capture_session(t)
    t.other_pokemon = inj
    t.own_pokemon = SimpleNamespace(pokemon=[mon, mon, mon])
    ok, idx = engine.local_inject_commit(t)
    assert ok and idx == 2, (ok, idx)
    assert link.sent.count(CTRL | (0x80 << 16) | 25) == 11, "offer sent 11x"
    payloads = [
        (0xA2 << 16) | 25,
        (0xB2 << 16) | 25,
        (0x90 << 16) | 25,
        (0x91 << 16) | (pid & 0xFFFF),
        (0x92 << 16) | (pid >> 16),
        (0x93 << 16) | 25,
        (0x94 << 16) | (pid & 0xFFFF),
        (0x95 << 16) | (pid >> 16),
        (0x9C << 16),
    ]
    for a in payloads:
        assert link.sent.count(CTRL | a) == 11, hex(a)
    print("  inject commit handshake (offer/2 accepts/7 successes)")

    link = ScriptedLink([(0x82 << 16) | 25, 0xA1 << 16] + [0x8F << 16]*10)
    t = engine.build_trader(link, verbose=False, sanity=True, gen=3)
    t.sleep_timer = 0; engine.prime_capture_session(t); t.other_pokemon = inj; t.own_pokemon = SimpleNamespace(pokemon=[mon, mon, mon])
    ok, idx = engine.local_inject_commit(t)
    assert not ok and idx == 2
    link = ScriptedLink([0x8F << 16] * 2)
    t = engine.build_trader(link, verbose=False, sanity=True, gen=3)
    t.sleep_timer = 0; engine.prime_capture_session(t); t.other_pokemon = inj
    ok, idx = engine.local_inject_commit(t)
    assert not ok and idx is None
    print("  decline + cancel paths abort cleanly")

print("PASS")
PY
