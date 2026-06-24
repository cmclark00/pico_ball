"""
Serialize a captured Gen 1 party into the vault.

For each Pokémon we write:
  * partyNN_<name>.pk1   -- raw 66-byte trade record: the 44-byte Gen 1
                            structure + 11-byte OT name + 11-byte nickname
                            (everything needed to faithfully re-trade it)
  * partyNN_<name>.json  -- decoded summary
and a combined party.json.
"""
import datetime
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


_GEN_FOLDER = {1: "rby", 2: "gsc", 3: "rse"}

# Gen 3 (Western) text codes for nicknames / OT names.
_G3_TEXT = {0x00: " ", 0xAB: "!", 0xAC: "?", 0xAD: ".", 0xAE: "-",
            0xB4: "'", 0xB5: "♂", 0xB6: "♀", 0xBA: "/"}
for _i in range(10):
    _G3_TEXT[0xA1 + _i] = chr(ord("0") + _i)
for _i in range(26):
    _G3_TEXT[0xBB + _i] = chr(ord("A") + _i)
    _G3_TEXT[0xD5 + _i] = chr(ord("a") + _i)


def _decode_g3_text(values):
    out = []
    for v in values:
        if v == 0xFF:  # terminator
            break
        out.append(_G3_TEXT.get(v, "?"))
    return "".join(out).strip()


def _load_species_names(engine_dir, gen=1):
    if gen == 3:
        # The engine's rse names file only covers ids < 256; use our baked
        # internal-id table (1..411) instead.
        from .gen3_data import SPECIES_NAMES_G3
        return SPECIES_NAMES_G3
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


def _decode_mon(mon, names, gen=1):
    species = mon.get_species()
    species_name = names[species] if 0 <= species < len(names) else f"#{species}"
    if not species_name:
        species_name = f"#{species}"
    raw = _ints(mon.get_data())

    def _try(fn, *a):
        try:
            return fn(*a)
        except Exception:  # noqa: BLE001 - decoding best-effort
            return None

    if gen == 3:
        # Gen 3 keeps the names inside the party struct (no Text sub-objects).
        nickname_vals = _ints(mon.values[mon.nickname_pos:mon.nickname_pos + mon.nickname_len])
        ot_vals = _ints(mon.values[mon.ot_name_pos:mon.ot_name_pos + mon.ot_name_len])
        nickname, ot_name = _decode_g3_text(nickname_vals), _decode_g3_text(ot_vals)
    else:
        nickname_vals = _ints(getattr(getattr(mon, "nickname", None), "values", []) or [])
        ot_vals = _ints(getattr(getattr(mon, "ot_name", None), "values", []) or [])
        nickname, ot_name = _decode_gb_text(nickname_vals), _decode_gb_text(ot_vals)

    info = {
        "species_index": species,
        "species_name": species_name,
        "nickname": nickname,
        "nickname_raw": nickname_vals,
        "ot_name": ot_name,
        "ot_name_raw": ot_vals,
        "level": _try(mon.get_level),
        "current_hp": _try(mon.get_curr_hp),
        "moves": [_try(mon.get_move, i) for i in range(4)],
        "pp": [_try(mon.get_pp, i) for i in range(4)],
        "raw_pk1_hex": bytes(raw).hex(),
    }
    if gen == 3:
        # The .pk3 file is the 100-byte party struct (PKHeX-importable); the
        # full engine record (struct + mail + version + ribbon) re-trades
        # losslessly and lives in the JSON.
        info["raw_pk1_hex"] = bytes(raw[:100]).hex()
        info["raw_record_hex"] = bytes(raw).hex()
    return info


_RECORD_EXT = {1: ".pk1", 2: ".pk2", 3: ".pk3"}

# Record file sizes (bytes) -> generation, for best-effort gen inference and the
# gen/record mismatch warning. Mirrors the WebUI's length-based detection.
# Gen 1 = 66 (44 struct + 11 OT + 11 nick); Gen 2 = 70 (no mail) or 117 (mail);
# Gen 3 .pk3 file = 100 (the PKHeX party struct).
_LEN_GEN = {66: 1, 70: 2, 117: 2, 100: 3}


def record_ext(gen):
    """The vault file extension for a generation (.pk1/.pk2/.pk3)."""
    return _RECORD_EXT.get(gen, ".pk1")


def infer_gen(path):
    """Best-effort generation of a vault record. Length is authoritative when
    recognised (it disambiguates legacy Gen 2 files saved as .pk1); otherwise
    fall back to the extension. Returns an int gen or None if unknown."""
    try:
        n = os.path.getsize(path)
    except OSError:
        n = None
    if n in _LEN_GEN:
        return _LEN_GEN[n]
    return {".pk1": 1, ".pk2": 2, ".pk3": 3}.get(os.path.splitext(path)[1].lower())


def _write_record(info, vault_dir, stem, gen=1):
    # Stamp when this record was saved (local time), so the WebUI/`vault list`
    # can show a capture date. On the standalone deck the device RTC could
    # supply this instead; here it's the host clock at save time.
    info.setdefault("captured", datetime.datetime.now().isoformat(timespec="seconds"))
    ext = _RECORD_EXT.get(gen, ".pk1")
    rec_path = os.path.join(vault_dir, stem + ext)
    with open(rec_path, "wb") as fh:
        fh.write(bytes.fromhex(info["raw_pk1_hex"]))
    json_path = os.path.join(vault_dir, stem + ".json")
    with open(json_path, "w", encoding="utf-8") as fh:
        json.dump(info, fh, indent=2)
    return rec_path


def save_party_member(party, index, vault_dir, engine_dir, prefix="mon", gen=1):
    """Save a single party member; returns the .pk1 path written.

    If a file with the chosen stem already exists, a numeric suffix is added so
    repeated injections never overwrite earlier captures.
    """
    os.makedirs(vault_dir, exist_ok=True)
    names = _load_species_names(engine_dir, gen)
    info = _decode_mon(party.pokemon[index], names, gen=gen)
    ext = _RECORD_EXT.get(gen, ".pk1")
    base_stem = f"{prefix}_{_safe(info['species_name'])}"
    stem, n = base_stem, 1
    while os.path.exists(os.path.join(vault_dir, stem + ext)):
        stem = f"{base_stem}_{n}"
        n += 1
    return _write_record(info, vault_dir, stem, gen=gen)


def save_party(party, vault_dir, engine_dir, gen=1):
    """Write every Pokémon in `party` into vault_dir."""
    os.makedirs(vault_dir, exist_ok=True)
    names = _load_species_names(engine_dir, gen)
    size = party.get_party_size()

    ext = record_ext(gen)
    summary = {"party_size": size, "pokemon": []}
    written = []
    for i in range(size):
        mon = party.pokemon[i]
        info = _decode_mon(mon, names, gen=gen)
        # Never overwrite an earlier capture: if the stem is taken, add a
        # numeric suffix (same policy as save_party_member).
        base_stem = f"party{i:02d}_{_safe(info['species_name'])}"
        stem, n = base_stem, 1
        while os.path.exists(os.path.join(vault_dir, stem + ext)):
            stem = f"{base_stem}_{n}"
            n += 1
        _write_record(info, vault_dir, stem, gen=gen)
        summary["pokemon"].append(info)
        written.append((stem, info))

    with open(os.path.join(vault_dir, "party.json"), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2)

    return written
