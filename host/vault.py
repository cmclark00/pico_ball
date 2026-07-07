#!/usr/bin/env python3
"""
vault.py -- show everything captured in the vault, decoded.

Reads the records in vault/ and its subfolders (.pk1/.pk2/.pk3 — including
dex/ from import_standalone.py and dex/gen3/ PCCS conversions) plus their
sibling .json summaries (written by extract.py / inject.py) and prints a
single table, grouped by generation. No device needed.

Usage:
    python host/vault.py                 # list vault/
    python host/vault.py --out some/dir  # a different vault directory
    python host/vault.py --json          # machine-readable dump
"""
import argparse
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from picovault import savedata  # noqa: E402

_RECORD_GLOBS = ("*.pk1", "*.pk2", "*.pk3")


def _records(vault_dir):
    """Yield dicts describing each record file (vault_dir and subfolders like
    dex/), decoded from its sibling JSON where available."""
    paths = sorted(
        {p for g in _RECORD_GLOBS
         for p in glob.glob(os.path.join(vault_dir, "**", g), recursive=True)}
    )
    for path in paths:
        gen = savedata.infer_gen(path)
        info = {}
        json_path = os.path.splitext(path)[0] + ".json"
        if os.path.isfile(json_path):
            try:
                with open(json_path, encoding="utf-8") as fh:
                    info = json.load(fh)
            except (OSError, ValueError):
                info = {}
        moves = [m for m in (info.get("moves") or []) if m]
        captured = (info.get("captured") or "")[:10]   # date portion of the ISO stamp
        yield {
            "file": os.path.relpath(path, vault_dir),
            "gen": gen,
            "species": info.get("species_name") or "?",
            "level": info.get("level"),
            "nickname": info.get("nickname") or "",
            "ot": info.get("ot_name") or "",
            "moves": len(moves),
            "captured": captured,
            "size": os.path.getsize(path),
        }


def _print_table(rows):
    if not rows:
        print("Vault is empty. Capture something with `extract.py` first.")
        return
    hdr = ("#", "Gen", "Species", "Lv", "Nickname", "OT", "Mv", "Captured", "File")
    widths = [len(h) for h in hdr]
    table = []
    for i, r in enumerate(rows):
        cells = (
            str(i),
            f"G{r['gen']}" if r["gen"] else "?",
            r["species"][:14],
            str(r["level"]) if r["level"] is not None else "-",
            r["nickname"][:10],
            r["ot"][:10],
            str(r["moves"]),
            r["captured"] or "-",
            r["file"],
        )
        widths = [max(w, len(c)) for w, c in zip(widths, cells)]
        table.append(cells)

    fmt = "  ".join(f"{{:<{w}}}" for w in widths)
    print(fmt.format(*hdr))
    print(fmt.format(*("-" * w for w in widths)))
    for cells in table:
        print(fmt.format(*cells))

    per_gen = {}
    for r in rows:
        per_gen[r["gen"]] = per_gen.get(r["gen"], 0) + 1
    tally = ", ".join(f"Gen {g or '?'}: {n}" for g, n in sorted(
        per_gen.items(), key=lambda kv: (kv[0] is None, kv[0])))
    print(f"\n{len(rows)} record(s)  ({tally})")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault"),
                    help="vault directory (default: ./vault)")
    ap.add_argument("--json", action="store_true",
                    help="emit JSON instead of a table")
    args = ap.parse_args()

    rows = list(_records(args.out))
    if args.json:
        json.dump(rows, sys.stdout, indent=2)
        print()
    else:
        _print_table(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
