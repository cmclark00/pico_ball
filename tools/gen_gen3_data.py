#!/usr/bin/env python3
"""
Generate the Gen 3 (RSE/FRLG) lookup tables:
  * host/picovault/gen3_data.py -- species names by internal id (for savedata)
  * webui/pokedata_gen3.js      -- species/moves/types/items/gender tables for
                                   the in-browser decoder

Gen 3 records store the *internal* species id (1..411): 1..251 match the
national dex; 252..276 are unused placeholders; 277..411 are the Hoenn species
in internal order. The internal->national deltas are PKHeX's
Table3InternalToNational (SpeciesConverter.cs). Everything else comes from the
PokeAPI CSVs, fetched at generation time.
"""
import csv
import io
import os
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
OUT = os.path.join(REPO_ROOT, "host", "picovault", "gen3_data.py")
OUT_JS = os.path.join(REPO_ROOT, "webui", "pokedata_gen3.js")

CSV_BASE = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/"
SPECIES_CSV_URL = CSV_BASE + "pokemon_species.csv"

# PKHeX SpeciesConverter.Table3InternalToNational: national = internal + delta,
# for internal ids 277..411.
DELTAS = [
    -25, -25, -25,
    -25, -25, -25, -25, -25, -25, -25, -25, -25, -25,
    -25, -25, -25, -25, -25, -25, -25, -25, -25, -25,
    -25, -11, -11, -11, -28, -28, -21, -21, 19, -31,
    -31, -28, -28, 7, 7, -15, -15, 35, 25, 25,
    -21, 3, -20, 16, 16, 45, 15, 15, 21, 21,
    -12, -12, -4, -4, -4, -39, -39, -28, -28, -17,
    -17, 22, 22, 22, -13, -13, 15, 15, -11, -11,
    -52, -26, -26, -42, -42, -52, -49, -49, -25, -25,
    0, -6, -6, -48, -77, -77, -77, -51, -51, -12,
    -77, -77, -77, -7, -7, -7, -17, -24, -24, -43,
    -45, -12, -78, -78, -78, -34, -73, -73, -43, -43,
    -43, -43, -112, -112, -112, -24, -24, -24, -24, -24,
    -24, -24, -24, -24, -22, -22, -22, -27, -27, -24,
    -24, -53,
]
assert len(DELTAS) == 135, len(DELTAS)

# PokeAPI identifiers that don't title-case into the real display name.
NAME_FIXES = {
    "nidoran-f": "Nidoran♀", "nidoran-m": "Nidoran♂", "farfetchd": "Farfetch'd",
    "mr-mime": "Mr. Mime", "ho-oh": "Ho-Oh", "porygon2": "Porygon2",
}


def _csv_rows(name):
    with urllib.request.urlopen(CSV_BASE + name, timeout=30) as resp:
        rows = list(csv.reader(io.TextIOWrapper(resp, encoding="utf-8")))
    hdr = rows[0]
    return hdr, rows[1:]


def national_names():
    hdr, rows = _csv_rows("pokemon_species.csv")
    idi, ni = hdr.index("id"), hdr.index("identifier")
    names = [""] * 387
    for r in rows:
        i = int(r[idi])
        if 1 <= i <= 386:
            ident = r[ni]
            names[i] = NAME_FIXES.get(ident, ident.replace("-", " ").title())
    assert all(names[1:]), "missing national names"
    return names


def gender_rates():
    """veekun gender_rate by national dex 1..386 (-1 genderless, else female
    eighths). Gen 3 gender: female iff (PID & 0xFF) < rate*32 - 1 thresholds."""
    hdr, rows = _csv_rows("pokemon_species.csv")
    idi, gi = hdr.index("id"), hdr.index("gender_rate")
    rates = [-1] * 387
    for r in rows:
        i = int(r[idi])
        if 1 <= i <= 386:
            rates[i] = int(r[gi])
    return rates


def move_names():
    """Move names by id 1..354 (Gen 3's move ids match the modern ids)."""
    hdr, rows = _csv_rows("moves.csv")
    idi, ni = hdr.index("id"), hdr.index("identifier")
    names = [""] * 355
    for r in rows:
        i = int(r[idi])
        if 1 <= i <= 354:
            names[i] = r[ni].replace("-", " ").title()
    return names


def types_by_dex():
    """[type names] by national dex 1..386, adjusted to Gen 3 (no Fairy)."""
    hdr, rows = _csv_rows("types.csv")
    idi, ni = hdr.index("id"), hdr.index("identifier")
    tname = {int(r[idi]): r[ni].title() for r in rows}
    hdr, rows = _csv_rows("pokemon_types.csv")
    pi, ti, si = hdr.index("pokemon_id"), hdr.index("type_id"), hdr.index("slot")
    types = [[] for _ in range(387)]
    for r in sorted(rows, key=lambda r: (int(r[0]), int(r[2]))):
        p = int(r[pi])
        if 1 <= p <= 386:
            n = tname.get(int(r[ti]), "")
            if n and n != "Fairy":          # Fairy didn't exist in Gen 3
                types[p].append(n)
    for p in range(1, 387):
        if not types[p]:                    # pure-Fairy mons were Normal then
            types[p] = ["Normal"]
    return types


def gen3_item_names():
    """Item names by Gen 3 game index (the id stored in the mon struct)."""
    hdr, rows = _csv_rows("items.csv")
    idi, ni = hdr.index("id"), hdr.index("identifier")
    iname = {int(r[idi]): r[ni].replace("-", " ").title() for r in rows}
    hdr, rows = _csv_rows("item_game_indices.csv")
    ii, gi, xi = hdr.index("item_id"), hdr.index("generation_id"), hdr.index("game_index")
    out = [""] * 377                        # last_valid_item = 376
    for r in rows:
        if int(r[gi]) == 3 and 0 < int(r[xi]) <= 376:
            out[int(r[xi])] = iname.get(int(r[ii]), "")
    return out


def main():
    nat = national_names()
    internal = [""] * 412
    int2nat = [0] * 412
    for i in range(1, 252):
        internal[i], int2nat[i] = nat[i], i
    for i in range(277, 412):
        n = i + DELTAS[i - 277]
        internal[i], int2nat[i] = nat[n], n

    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write('"""AUTO-GENERATED by tools/gen_gen3_data.py -- do not edit.\n\n'
                 "Gen 3 internal species id (1..411) -> display name. Unused\n"
                 'internal ids (252..276) are empty strings."""\n\n')
        fh.write("SPECIES_NAMES_G3 = [\n")
        for i in range(0, 412, 4):
            row = ", ".join(repr(internal[j]) if internal[j] else "''"
                            for j in range(i, min(i + 4, 412)))
            fh.write(f"    {row},\n")
        fh.write("]\n")
    print(f"Wrote {OUT} (412 internal ids; Treecko@277={internal[277]}, "
          f"Chimecho@411={internal[411]})")

    moves = move_names()
    types = types_by_dex()
    rates = gender_rates()
    items = gen3_item_names()

    def q(s):
        return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'

    def js_str_array(name, vals, per=6):
        body = ",\n  ".join(", ".join(q(x) for x in vals[i:i + per])
                            for i in range(0, len(vals), per))
        return f"const {name} = [\n  {body}\n];"

    def js_num_array(name, vals, per=16):
        body = ",\n  ".join(", ".join(str(x) for x in vals[i:i + per])
                            for i in range(0, len(vals), per))
        return f"const {name} = [\n  {body}\n];"

    def js_nested(name, rows):
        body = ",\n  ".join("[" + ", ".join(q(x) for x in row) + "]" for row in rows)
        return f"const {name} = [\n  {body}\n];"

    exported = ["SPECIES_NAMES_G3", "INTERNAL_TO_NATIONAL_G3", "MOVE_NAMES_G3",
                "TYPES_G3", "GENDER_RATE_G3", "ITEM_NAMES_G3"]
    with open(OUT_JS, "w", encoding="utf-8") as fh:
        fh.write("// AUTO-GENERATED by tools/gen_gen3_data.py -- do not edit.\n")
        fh.write("// Species names indexed by Gen 3 INTERNAL id; types/gender by\n"
                 "// NATIONAL dex (use INTERNAL_TO_NATIONAL_G3); items by Gen 3\n"
                 "// game index; moves by move id.\n")
        fh.write(js_str_array("SPECIES_NAMES_G3", internal) + "\n\n")
        fh.write(js_num_array("INTERNAL_TO_NATIONAL_G3", int2nat) + "\n\n")
        fh.write(js_str_array("MOVE_NAMES_G3", moves) + "\n\n")
        fh.write(js_nested("TYPES_G3", types) + "\n\n")
        fh.write(js_num_array("GENDER_RATE_G3", rates) + "\n\n")
        fh.write(js_str_array("ITEM_NAMES_G3", items) + "\n")
        fh.write("\nif (typeof globalThis !== 'undefined') {\n"
                 + "".join(f"  globalThis.{n} = {n};\n" for n in exported)
                 + "}\n"
                 f"if (typeof module !== 'undefined') module.exports = "
                 f"{{ {', '.join(exported)} }};\n")
    print(f"Wrote {OUT_JS} ({len(moves)-1} moves, {len(items)-1} item indices)")


if __name__ == "__main__":
    main()
