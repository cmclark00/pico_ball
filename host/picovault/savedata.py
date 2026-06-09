"""
Serialize a captured Gen 1 party into the vault.

For each Pokémon we write:
  * partyNN_<name>.pk1   -- raw 66-byte trade record: the 44-byte Gen 1
                            structure + 11-byte OT name + 11-byte nickname
                            (everything needed to faithfully re-trade it)
  * partyNN_<name>.json  -- decoded summary
and a combined party.json.
"""
import json
import os

# Minimal Gen 1 (English) text decoder for nicknames / OT names.
# Enough for readability; the raw GB codes are also stored.
_GB_TEXT = {0x50: "", 0x7F: " ", 0x7C: "to", 0x74: "<...>"}
for _i in range(26):
    _GB_TEXT[0x80 + _i] = chr(ord("A") + _i)
    _GB_TEXT[0xA0 + _i] = chr(ord("a") + _i)
for _i in range(10):
    _GB_TEXT[0xF6 + _i] = chr(ord("0") + _i)


def _decode_gb_text(values):
    out = []
    for v in values:
        if v == 0x50:  # terminator
            break
        out.append(_GB_TEXT.get(v, "?"))
    return "".join(out).strip()


_GEN_FOLDER = {1: "rby", 2: "gsc"}


def _load_species_names(engine_dir, gen=1):
    path = os.path.join(engine_dir, "useful_data", _GEN_FOLDER.get(gen, "rby"),
                        "pokemon_names.txt")
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return [line.rstrip("\n") for line in fh]
    except OSError:
        return []


def _ints(seq):
    return [int(x) & 0xFF for x in seq]


def _safe(name):
    return "".join(c if c.isalnum() else "_" for c in name) or "mon"


def _decode_mon(mon, names):
    species = mon.get_species()
    species_name = names[species] if 0 <= species < len(names) else f"#{species}"
    raw = _ints(mon.get_data())

    def _try(fn, *a):
        try:
            return fn(*a)
        except Exception:  # noqa: BLE001 - decoding best-effort
            return None

    nickname_vals = _ints(getattr(getattr(mon, "nickname", None), "values", []) or [])
    ot_vals = _ints(getattr(getattr(mon, "ot_name", None), "values", []) or [])

    return {
        "species_index": species,
        "species_name": species_name,
        "nickname": _decode_gb_text(nickname_vals),
        "nickname_raw": nickname_vals,
        "ot_name": _decode_gb_text(ot_vals),
        "ot_name_raw": ot_vals,
        "level": _try(mon.get_level),
        "current_hp": _try(mon.get_curr_hp),
        "moves": [_try(mon.get_move, i) for i in range(4)],
        "pp": [_try(mon.get_pp, i) for i in range(4)],
        "raw_pk1_hex": bytes(raw).hex(),
    }


def _write_record(info, vault_dir, stem):
    pk1_path = os.path.join(vault_dir, stem + ".pk1")
    with open(pk1_path, "wb") as fh:
        fh.write(bytes.fromhex(info["raw_pk1_hex"]))
    json_path = os.path.join(vault_dir, stem + ".json")
    with open(json_path, "w", encoding="utf-8") as fh:
        json.dump(info, fh, indent=2)
    return pk1_path


def save_party_member(party, index, vault_dir, engine_dir, prefix="mon", gen=1):
    """Save a single party member; returns the .pk1 path written.

    If a file with the chosen stem already exists, a numeric suffix is added so
    repeated injections never overwrite earlier captures.
    """
    os.makedirs(vault_dir, exist_ok=True)
    names = _load_species_names(engine_dir, gen)
    info = _decode_mon(party.pokemon[index], names)
    base_stem = f"{prefix}_{_safe(info['species_name'])}"
    stem, n = base_stem, 1
    while os.path.exists(os.path.join(vault_dir, stem + ".pk1")):
        stem = f"{base_stem}_{n}"
        n += 1
    return _write_record(info, vault_dir, stem)


def save_party(party, vault_dir, engine_dir, gen=1):
    """Write every Pokémon in `party` into vault_dir."""
    os.makedirs(vault_dir, exist_ok=True)
    names = _load_species_names(engine_dir, gen)
    size = party.get_party_size()

    summary = {"party_size": size, "pokemon": []}
    written = []
    for i in range(size):
        mon = party.pokemon[i]
        info = _decode_mon(mon, names)
        stem = f"party{i:02d}_{_safe(info['species_name'])}"

        pk1_path = os.path.join(vault_dir, stem + ".pk1")
        with open(pk1_path, "wb") as fh:
            fh.write(bytes.fromhex(info["raw_pk1_hex"]))

        json_path = os.path.join(vault_dir, stem + ".json")
        with open(json_path, "w", encoding="utf-8") as fh:
            json.dump(info, fh, indent=2)

        summary["pokemon"].append(info)
        written.append((stem, info))

    with open(os.path.join(vault_dir, "party.json"), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2)

    return written
