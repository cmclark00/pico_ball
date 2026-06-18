"""Bridge to the Pokemon Community Conversion Standard (third_party/PCCS).

Converts a captured Gen 1/2 trade record into a Gen 3 box .pk3 (80 bytes,
PKHeX-importable) by shelling out to the `pccs_convert` host binary that
scripts/setup.sh builds via tools/build_pccs.sh.

This is a *recreation*, mirroring Poké Transporter GB's Pal-Park-style transfer
(see Poke_Transporter_GB / PCCS, MIT): the converted mon gets a fabricated
PID / IVs / nature / ability while keeping species, level, moves, nickname, and
OT. It is not a literal "transfer" — Game Boy and GBA games have no native link.
"""
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
PCCS_BIN = os.path.join(REPO_ROOT, "third_party", "PCCS", "pccs_convert")

# Per-gen record geometry, matching utilities.*_trading_data_utils get_data():
#   record = full party struct[STRUCT] + OT[11] + nickname[11] (+ mail, ignored)
# PCCS reads only the leading box struct (BOX bytes) of the party struct.
_BOX_LEN = {1: 33, 2: 32}
_STRUCT_LEN = {1: 0x2C, 2: 0x30}
_NAME_LEN = 11


class PCCSUnavailable(RuntimeError):
    pass


def available():
    return os.path.exists(PCCS_BIN) and os.access(PCCS_BIN, os.X_OK)


def convert_record(gen, raw, sanitize_mythicals=True):
    """Convert a Gen 1/2 vault record (bytes/list) to an 80-byte Gen 3 .pk3.

    Returns the 80-byte box record, or None if PCCS rejected the mon (e.g. a
    blank/scaffold slot, MissingNo edge cases it won't map). Raises
    PCCSUnavailable if the converter hasn't been built.
    """
    if gen not in _BOX_LEN:
        raise ValueError(f"PCCS conversion only supports Gen 1/2, not Gen {gen}")
    if not available():
        raise PCCSUnavailable(
            "pccs_convert not built. Run scripts/setup.sh (or tools/build_pccs.sh)."
        )

    raw = bytes(raw)
    box_len, struct_len = _BOX_LEN[gen], _STRUCT_LEN[gen]
    need = struct_len + 2 * _NAME_LEN
    if len(raw) < need:
        raise ValueError(
            f"Gen {gen} record is {len(raw)} bytes; need >= {need} "
            "(struct + OT + nickname)."
        )

    box = raw[:box_len]
    ot = raw[struct_len:struct_len + _NAME_LEN]
    nick = raw[struct_len + _NAME_LEN:struct_len + 2 * _NAME_LEN]
    # PCCS's loadData order is (data, nickname, OT); our record stores OT first.
    cli_in = box + nick + ot

    args = [PCCS_BIN, str(gen)]
    if not sanitize_mythicals:
        args.append("--no-sanitize-mythicals")
    proc = subprocess.run(args, input=cli_in, capture_output=True)
    if proc.returncode == 1:
        return None  # converter rejected the mon
    if proc.returncode != 0:
        raise RuntimeError(
            f"pccs_convert failed (rc={proc.returncode}): {proc.stderr.decode().strip()}"
        )
    pk3 = bytes(int(b, 16) for b in proc.stdout.decode().split())
    if len(pk3) != 80:
        raise RuntimeError(f"pccs_convert returned {len(pk3)} bytes, expected 80")
    return pk3
