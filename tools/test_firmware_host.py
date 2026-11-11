#!/usr/bin/env python3
"""
Host-side tests for the standalone (Path B) firmware's Gen 1/2 logic. The real
firmware sources are compiled for the host and exercised without hardware:

  1. Trade FSM (capture.c + inject.c) against a simulated cartridge
     -> firmware/path-b-standalone/test/test_trade_fsm.c
  2. Gen 1 <-> Gen 2 conversion (cross_gen.c) for all 151 species, checked
     against the pinned trade engine's species names, types and stat formula.
  3. cross_gen_tables.h is what tools/gen_cross_gen.py would generate.

Needs a C compiler ($CC, cc/gcc/clang, or MSVC's cl on Windows) and the engine
from scripts/setup.sh.

    python tools/test_firmware_host.py
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
FW = os.path.join(REPO_ROOT, "firmware", "path-b-standalone")
sys.path.insert(0, os.path.join(REPO_ROOT, "host"))

from picovault import engine  # noqa: E402


# --- compiling the firmware sources for the host ------------------------------
def _find_vcvars():
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", ""),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return None
    out = subprocess.run([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                         capture_output=True, text=True).stdout.strip().splitlines()
    for root in out:
        bat = os.path.join(root, "VC", "Auxiliary", "Build", "vcvars64.bat")
        if os.path.isfile(bat):
            return bat
    return None


def compile_c(sources, exe, workdir):
    """Compile `sources` (paths relative to the firmware dir) into `exe`."""
    paths = [os.path.join(FW, s) for s in sources]
    cc = os.environ.get("CC") or next(
        (c for c in ("cc", "gcc", "clang") if shutil.which(c)), None)
    if cc:
        cmd = [cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-I", FW, "-o", exe, *paths]
        subprocess.run(cmd, check=True)
        return
    vcvars = _find_vcvars() if os.name == "nt" else None
    if not vcvars:
        raise SystemExit("No C compiler found (set CC, or install gcc/clang/MSVC).")
    args = ["cl", "/nologo", "/utf-8", "/std:c11", "/W3", "/O1", f"/I{FW}",
            f"/Fe:{exe}", f"/Fo:{workdir}{os.sep}", *paths]
    script = os.path.join(workdir, "build.bat")
    with open(script, "w") as fh:
        fh.write(f'@call "{vcvars}" >nul\r\n@' + subprocess.list2cmdline(args) + "\r\n")
    res = subprocess.run(["cmd", "/c", script], capture_output=True, text=True)
    if res.returncode != 0:
        sys.stdout.write(res.stdout + res.stderr)
        raise SystemExit("compile failed")


# --- 1. trade FSM vs simulated cartridge --------------------------------------
def test_trade_fsm(workdir):
    exe = os.path.join(workdir, "test_trade_fsm" + (".exe" if os.name == "nt" else ""))
    compile_c(["test/test_trade_fsm.c", "capture.c", "inject.c", "gen_profile.c",
               "mon_identity.c"], exe, workdir)
    res = subprocess.run([exe], capture_output=True, text=True)
    sys.stdout.write(res.stdout)
    if res.returncode != 0:
        sys.stdout.write(res.stderr)
        raise SystemExit("trade FSM test failed")


# --- 2. cross-gen conversion vs the engine ------------------------------------
class _FakeLink:
    def send_byte(self, b, n): pass
    def receive_byte(self, n=None): return 0


G1_LEN, G2_LEN, NAME_LEN = 0x2C, 0x30, 0x0B


def _convert(exe, requests):
    """requests: list of (direction, bytes) -> list of bytes | None."""
    text = "".join(f"{d} {bytes(r).hex()}\n" for d, r in requests)
    res = subprocess.run([exe], input=text, capture_output=True, text=True, check=True)
    lines = res.stdout.split()
    assert len(lines) == len(requests), (len(lines), len(requests))
    return [None if ln == "FAIL" else list(bytes.fromhex(ln)) for ln in lines]


def _g1_record(idx, rng, types, moves, pps):
    st = [0] * G1_LEN
    st[0x00] = idx
    st[0x04] = rng.choice([0, 0, 0x08, 0x40])            # status
    st[0x05], st[0x06] = types
    st[0x08:0x0C] = moves
    st[0x0C:0x0E] = [rng.randrange(256), rng.randrange(256)]             # OT ID
    st[0x0E:0x11] = [rng.randrange(4), rng.randrange(256), rng.randrange(256)]  # EXP
    for i in range(5):                                    # stat EXP, incl. the extremes
        v = rng.choice([0, 1, 0xFFFF, rng.randrange(0x10000)])
        st[0x11 + 2 * i:0x13 + 2 * i] = [v >> 8, v & 0xFF]
    st[0x1B:0x1D] = [rng.randrange(256), rng.randrange(256)]             # DVs
    st[0x1D:0x21] = pps
    level = rng.choice([1, 2, 5, 50, 100, rng.randrange(2, 101)])
    st[0x03] = st[0x21] = level
    ot = [0x80 + rng.randrange(26) for _ in range(7)] + [0x50] * 4
    nick = [0x80 + rng.randrange(26) for _ in range(10)] + [0x50]
    return st + ot + nick


def test_cross_gen(workdir):
    exe = os.path.join(workdir, "xg_filter" + (".exe" if os.name == "nt" else ""))
    compile_c(["test/xg_filter.c", "cross_gen.c"], exe, workdir)

    with engine.engine_cwd():
        for gen in (1, 2):   # loads each generation's data tables
            engine.build_trader(_FakeLink(), verbose=False, sanity=True, gen=gen)
        from utilities.gsc_trading_data_utils import GSCTradingPokémonInfo
        from utilities.rby_trading_data_utils import RBYTradingPokémonInfo, RBYUtils

        def names(folder):
            with open(os.path.join("useful_data", folder, "pokemon_names.txt"),
                      encoding="utf-8") as fh:
                return fh.read().splitlines()[:256]
        rby_names, gsc_names = names("rby"), names("gsc")
        real = [i for i in range(1, 256)
                if rby_names[i] in gsc_names[1:152]]
        assert len(real) == 151, len(real)

        # Every Gen 1 species, with engine-consistent stats.
        g1_recs = []
        for idx in real:
            rng = random.Random(idx)
            moves = [rng.randrange(1, 166) for _ in range(rng.randrange(1, 5))]
            pps = [(rng.randrange(4) << 6) | rng.randrange(1, 36) for _ in moves]
            moves += [0] * (4 - len(moves))
            pps += [0] * (4 - len(pps))
            rec = _g1_record(idx, rng, list(RBYUtils.types_list[idx]), moves, pps)
            mon = RBYTradingPokémonInfo.set_data(rec)
            mon.update_stats()                      # the engine's Gen 1 stat formula
            g1_recs.append(list(mon.get_data())[:G1_LEN + 2 * NAME_LEN])

        g2_recs = _convert(exe, [("12", r) for r in g1_recs])
        failures = 0
        for idx, g1, g2 in zip(real, g1_recs, g2_recs):
            name = rby_names[idx]
            if g2 is None:
                print(f"  FAIL {name}: Gen 1 -> Gen 2 refused"); failures += 1; continue
            if gsc_names[g2[0]] != name:
                print(f"  FAIL {name}: became {gsc_names[g2[0]]} in Gen 2"); failures += 1
            mon = GSCTradingPokémonInfo.set_data(list(g2))
            mon.update_stats()                      # the engine's Gen 2 stat formula
            if list(mon.values) != g2[:G2_LEN]:
                print(f"  FAIL {name}: Gen 2 stats/HP not what the engine computes")
                failures += 1
            if g2[G2_LEN:] != g1[G1_LEN:]:
                print(f"  FAIL {name}: names changed"); failures += 1

        # ...and back again must reproduce the original record exactly.
        back = _convert(exe, [("21", r) for r in g2_recs if r is not None])
        for idx, g1, b in zip(real, g1_recs, back):
            if b != g1:
                diff = [hex(i) for i in range(len(g1)) if b is None or b[i] != g1[i]]
                print(f"  FAIL {rby_names[idx]}: Gen 1 -> 2 -> 1 round trip differs at {diff}")
                failures += 1

        # Gen 2-only content must be dropped or refused, never passed through.
        base = [r for r in g2_recs if r is not None][0]
        def g2_with(species=None, moves=None, pps=None):
            r = list(base)
            if species is not None: r[0] = species
            if moves is not None: r[2:6] = moves
            if pps is not None: r[0x17:0x1B] = pps
            return r
        cases = _convert(exe, [
            ("21", g2_with(moves=[200, 33, 250, 45], pps=[0x41, 0x82, 0x03, 0xC4])),
            ("21", g2_with(moves=[200, 250, 251, 0])),
            ("21", g2_with(species=152)),
            ("21", g2_with(species=251)),
            ("21", g2_with(species=0)),
            ("12", [0x1F] + g1_recs[0][1:]),     # MissingNo.
            ("12", [0x00] + g1_recs[0][1:]),
            ("12", [0xFF] + g1_recs[0][1:]),
        ])
        mixed = cases[0]
        if mixed is None or mixed[0x08:0x0C] != [33, 45, 0, 0] or mixed[0x1D:0x21] != [0x82, 0xC4, 0, 0]:
            print("  FAIL: Gen 2-only moves not dropped/packed:", mixed and mixed[0x08:0x0C])
            failures += 1
        for label, got in zip(["only Gen 2 moves", "Chikorita", "Celebi", "species 0",
                               "MissingNo.", "Gen 1 species 0x00", "Gen 1 species 0xFF"],
                              cases[1:]):
            if got is not None:
                print(f"  FAIL: {label} should be refused"); failures += 1

    if failures:
        raise SystemExit(f"cross-gen conversion: {failures} check(s) failed")
    print(f"cross-gen conversion: all {len(real)} species round-trip and match the engine")


def test_tables_current():
    res = subprocess.run([sys.executable, os.path.join(HERE, "gen_cross_gen.py"), "--check"],
                         capture_output=True, text=True)
    sys.stdout.write(res.stdout + res.stderr)
    if res.returncode != 0:
        raise SystemExit("cross_gen_tables.h is stale")


def main():
    workdir = tempfile.mkdtemp(prefix="pico_ball_fwtest_")
    try:
        test_tables_current()
        test_trade_fsm(workdir)
        test_cross_gen(workdir)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
