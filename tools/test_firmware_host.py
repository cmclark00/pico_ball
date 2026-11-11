#!/usr/bin/env python3
"""
Host-side tests for the standalone (Path B) firmware's Gen 1/2 logic. The real
firmware sources are compiled for the host and exercised without hardware:

  1. Trade FSM (capture.c + inject.c) against a simulated cartridge
     -> firmware/path-b-standalone/test/test_trade_fsm.c
  2. Gen 1 <-> Gen 2 conversion (cross_gen.c) for all 151 species, checked
     against the pinned trade engine's species names, types and stat formula.
  3. cross_gen_tables.h / gen3_stats_tables.h are what their generators produce.
  4. On-device Gen 1/2 -> Gen 3 conversion (pccs_bridge.cpp + PCCS + gen3_party.c)
     for every Gen 1 and Gen 2 species, checked with the engine's Gen 3 parser.
     Skipped when PCCS isn't set up (third_party/PCCS + tools/gen_pccs_firmware.py).

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


def compile_mixed(c_sources, cxx_sources, include_dirs, exe, workdir):
    """Compile C + C++ sources (absolute paths) into `exe`."""
    cc = os.environ.get("CC") or next(
        (c for c in ("cc", "gcc", "clang") if shutil.which(c)), None)
    cxx = os.environ.get("CXX") or next(
        (c for c in ("c++", "g++", "clang++") if shutil.which(c)), None)
    if cc and cxx:
        inc = [a for d in include_dirs for a in ("-I", d)]
        objs = []
        for src in c_sources:
            obj = os.path.join(workdir, os.path.basename(src) + ".o")
            subprocess.run([cc, "-std=c11", "-O1", "-Wall", *inc, "-c", src, "-o", obj], check=True)
            objs.append(obj)
        subprocess.run([cxx, "-std=c++17", "-O1", "-w", *inc, *cxx_sources, *objs, "-o", exe],
                       check=True)
        return
    vcvars = _find_vcvars() if os.name == "nt" else None
    if not vcvars:
        raise SystemExit("No C/C++ compiler found (set CC and CXX, or install gcc/clang/MSVC).")
    inc = [f"/I{d}" for d in include_dirs]
    objs = [os.path.join(workdir, os.path.splitext(os.path.basename(s))[0] + ".obj")
            for s in c_sources]
    steps = [
        ["cl", "/nologo", "/utf-8", "/std:c11", "/O1", "/c", *inc, f"/Fo:{workdir}{os.sep}",
         *c_sources],
        ["cl", "/nologo", "/utf-8", "/std:c++17", "/EHsc", "/O1", "/w", *inc,
         f"/Fe:{exe}", f"/Fo:{workdir}{os.sep}", *cxx_sources, *objs],
    ]
    script = os.path.join(workdir, "build_mixed.bat")
    with open(script, "w") as fh:
        fh.write(f'@call "{vcvars}" >nul\r\n')
        for step in steps:
            fh.write("@" + subprocess.list2cmdline(step) + " || exit /b 1\r\n")
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
    for gen_script in ("gen_cross_gen.py", "gen_gen3_stats.py"):
        res = subprocess.run([sys.executable, os.path.join(HERE, gen_script), "--check"],
                             capture_output=True, text=True)
        sys.stdout.write(res.stdout + res.stderr)
        if res.returncode != 0:
            raise SystemExit(f"a table generated by {gen_script} is stale")


# --- 4. on-device Gen 1/2 -> Gen 3 conversion ----------------------------------
PCCS_DIR = os.path.join(REPO_ROOT, "third_party", "PCCS")
PCCS_GEN = os.path.join(FW, "pccs_gen")


def _expected_gen3_stats(base, ivs, evs, level, pid):
    """The Gen 3 games' stat formula, in party order HP/Atk/Def/Spe/SpA/SpD."""
    nature = pid % 25
    up, down = nature // 5 + 1, nature % 5 + 1
    out = []
    for s in range(6):
        n = (2 * base[s] + ivs[s] + evs[s] // 4) * level // 100
        if s == 0:
            n += level + 10
        else:
            n += 5
            if up != down:
                if s == up:
                    n = n * 110 // 100
                elif s == down:
                    n = n * 90 // 100
        out.append(n)
    return out


def test_gen3_conversion(workdir):
    subprocess.run([sys.executable, os.path.join(HERE, "gen_pccs_firmware.py")],
                   capture_output=True, text=True)
    if not (os.path.isfile(os.path.join(PCCS_GEN, "pccs_tables.c"))
            and os.path.isfile(os.path.join(PCCS_DIR, "source", "GBPokemon.cpp"))):
        print("Gen 1/2 -> Gen 3 conversion: SKIPPED (PCCS not set up; run scripts/setup.sh)")
        return
    exe = os.path.join(workdir, "g3conv" + (".exe" if os.name == "nt" else ""))
    pccs_src = [os.path.join(PCCS_DIR, "source", n + ".cpp") for n in
                ("GBPokemon", "Gen1Pokemon", "Gen2Pokemon", "Gen3Pokemon", "Pokemon",
                 "pokemon_data")]
    compile_mixed(
        [os.path.join(FW, "gen3_party.c"), os.path.join(PCCS_GEN, "pccs_tables.c")],
        [os.path.join(FW, "test", "g3conv_filter.cpp"), os.path.join(FW, "pccs_bridge.cpp"),
         *pccs_src],
        [FW, PCCS_GEN, os.path.join(PCCS_DIR, "include")], exe, workdir)

    with engine.engine_cwd():
        for gen in (1, 2, 3):
            engine.build_trader(_FakeLink(), verbose=False, sanity=True, gen=gen)
        from utilities.gsc_trading_data_utils import GSCTradingPokémonInfo, GSCUtils
        from utilities.rby_trading_data_utils import RBYTradingPokémonInfo, RBYUtils
        from utilities.rse_sp_trading_data_utils import RSESPTradingPokémonInfo, RSESPUtils

        def names(folder):
            with open(os.path.join("useful_data", folder, "pokemon_names.txt"),
                      encoding="utf-8") as fh:
                return fh.read().splitlines()[:256]
        rby_names, gsc_names = names("rby"), names("gsc")
        with open(os.path.join("useful_data", "rse", "stats.bin"), "rb") as fh:
            rse_stats = fh.read()

        ot = [0x80, 0x92, 0x87] + [0x50] * 8            # "ASH"
        nick = [0x81, 0x94, 0x83, 0x83, 0x98] + [0x50] * 6   # "BUDDY"
        requests, meta = [], []
        for idx in range(1, 256):                        # every real Gen 1 species
            if rby_names[idx] not in gsc_names[1:152]:
                continue
            rng = random.Random(1000 + idx)
            level = rng.choice([2, 5, 17, 50, 100])
            st = [0] * 0x2C
            st[0x00] = idx
            st[0x03] = st[0x21] = level
            st[0x05], st[0x06] = RBYUtils.types_list[idx]
            st[0x08:0x0C] = [33, 0, 0, 0]                # Tackle
            st[0x1D:0x21] = [35, 0, 0, 0]
            st[0x0C:0x0E] = [rng.randrange(256), rng.randrange(256)]
            exp = RBYUtils.get_exp_level(idx, level, RBYUtils)
            st[0x0E:0x11] = [(exp >> 16) & 0xFF, (exp >> 8) & 0xFF, exp & 0xFF]
            st[0x1B:0x1D] = [rng.randrange(256), rng.randrange(256)]
            mon = RBYTradingPokémonInfo.set_data(st + ot + nick)
            mon.update_stats()
            requests.append((1, list(mon.get_data())[:0x2C + 22]))
            meta.append((1, rby_names[idx], gsc_names.index(rby_names[idx]), level))
        for dex in range(1, 252):                        # every Gen 2 species
            rng = random.Random(2000 + dex)
            level = rng.choice([2, 5, 17, 50, 100])
            st = [0] * 0x30
            st[0x00] = dex
            st[0x02:0x06] = [33, 0, 0, 0]
            st[0x17:0x1B] = [35, 0, 0, 0]
            st[0x06:0x08] = [rng.randrange(256), rng.randrange(256)]
            exp = GSCUtils.get_exp_level(dex, level, GSCUtils)
            st[0x08:0x0B] = [(exp >> 16) & 0xFF, (exp >> 8) & 0xFF, exp & 0xFF]
            st[0x15:0x17] = [rng.randrange(256), rng.randrange(256)]
            st[0x1B] = 70
            st[0x1F] = level
            mon = GSCTradingPokémonInfo.set_data(st + ot + nick)
            mon.update_stats()
            requests.append((2, list(mon.get_data())[:0x30 + 22]))
            meta.append((2, gsc_names[dex], dex, level))

        text = "".join(f"{g} {bytes(r).hex()}\n" for g, r in requests)
        res = subprocess.run([exe], input=text, capture_output=True, text=True, check=True)
        lines = res.stdout.split()
        assert len(lines) == len(requests), (len(lines), len(requests))

        failures = converted = 0
        for (gen, name, dex, level), line in zip(meta, lines):
            tag = f"Gen {gen} {name}"
            if line == "FAIL":
                print(f"  FAIL {tag}: conversion refused"); failures += 1; continue
            party = list(bytes.fromhex(line))
            converted += 1
            # The engine's own Gen 3 parser must accept it (checksum, species,
            # moves, ability) exactly as it accepts a mon captured from a cartridge.
            g3 = RSESPTradingPokémonInfo.set_data(party + [0] * 49)
            if not g3.is_valid:
                print(f"  FAIL {tag}: engine rejects the Gen 3 record"); failures += 1; continue
            if g3.get_species() != dex:
                print(f"  FAIL {tag}: became species {g3.get_species()}"); failures += 1
            if g3.get_is_egg():
                print(f"  FAIL {tag}: converted into an Egg"); failures += 1
            # PCCS replaces a Mew or Celebi it can't prove is a real event one
            # with its own event version (its "sanitize mythicals" default, as
            # in the host converter), so their level may legitimately change.
            if party[84] != level and dex not in (151, 251):
                print(f"  FAIL {tag}: level {party[84]} (was {level})"); failures += 1
            level = party[84]
            if party[84] != RSESPUtils.get_level_exp(dex, g3.get_exp(), RSESPUtils):
                print(f"  FAIL {tag}: level disagrees with the engine's exp table"); failures += 1
            row = rse_stats[dex * 6:dex * 6 + 6]         # file order HP Atk Def SpA SpD Spe
            base = [row[0], row[1], row[2], row[5], row[3], row[4]]
            iv_word = int.from_bytes(bytes(g3.misc[4:8]), "little")
            ivs = [(iv_word >> (5 * i)) & 0x1F for i in range(6)]
            want = _expected_gen3_stats(base, ivs, list(g3.evs[:6]), level, g3.pid)
            got = [party[88 + 2 * i] | (party[89 + 2 * i] << 8) for i in range(6)]
            if got != want:
                print(f"  FAIL {tag}: stats {got}, expected {want}"); failures += 1
            if (party[86] | (party[87] << 8)) != got[0] or party[80:84] != [0, 0, 0, 0] \
                    or party[85] != 0xFF:
                print(f"  FAIL {tag}: not healthy / status / mail fields wrong"); failures += 1

    if failures:
        raise SystemExit(f"Gen 1/2 -> Gen 3 conversion: {failures} check(s) failed")
    print(f"Gen 1/2 -> Gen 3 conversion: all {converted} species convert to valid party records")


def main():
    workdir = tempfile.mkdtemp(prefix="pico_ball_fwtest_")
    try:
        test_tables_current()
        test_trade_fsm(workdir)
        test_cross_gen(workdir)
        test_gen3_conversion(workdir)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
