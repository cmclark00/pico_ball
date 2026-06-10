#!/usr/bin/env python3
"""
Generate webui/pokedata.js: lookup tables the in-browser decoder needs.

  * SPECIES_NAMES -- indexed by Gen 1 *internal* index (from the engine's
    pokemon_names.txt; that's what the trade data stores).
  * MOVE_NAMES    -- Gen 1 move names, indexed by move id (baked here).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
ENGINE_DATA = os.path.join(REPO_ROOT, "third_party", "PokemonGB_Online_Trades", "useful_data")
ENGINE_NAMES = os.path.join(ENGINE_DATA, "rby", "pokemon_names.txt")
GEN2_NAMES = os.path.join(ENGINE_DATA, "gsc", "pokemon_names.txt")
OUT = os.path.join(REPO_ROOT, "webui", "pokedata.js")

# Gen 1 move names, id 0 (none) .. 165. Display-only.
MOVE_NAMES = [
    "—", "Pound", "Karate Chop", "Double Slap", "Comet Punch", "Mega Punch",
    "Pay Day", "Fire Punch", "Ice Punch", "Thunder Punch", "Scratch",
    "Vise Grip", "Guillotine", "Razor Wind", "Swords Dance", "Cut", "Gust",
    "Wing Attack", "Whirlwind", "Fly", "Bind", "Slam", "Vine Whip", "Stomp",
    "Double Kick", "Mega Kick", "Jump Kick", "Rolling Kick", "Sand Attack",
    "Headbutt", "Horn Attack", "Fury Attack", "Horn Drill", "Tackle",
    "Body Slam", "Wrap", "Take Down", "Thrash", "Double-Edge", "Tail Whip",
    "Poison Sting", "Twineedle", "Pin Missile", "Leer", "Bite", "Growl",
    "Roar", "Sing", "Supersonic", "Sonic Boom", "Disable", "Acid", "Ember",
    "Flamethrower", "Mist", "Water Gun", "Hydro Pump", "Surf", "Ice Beam",
    "Blizzard", "Psybeam", "Bubble Beam", "Aurora Beam", "Hyper Beam", "Peck",
    "Drill Peck", "Submission", "Low Kick", "Counter", "Seismic Toss", "Strength",
    "Absorb", "Mega Drain", "Leech Seed", "Growth", "Razor Leaf",
    "Solar Beam", "Poison Powder", "Stun Spore", "Sleep Powder", "Petal Dance",
    "String Shot", "Dragon Rage", "Fire Spin", "Thunder Shock", "Thunderbolt",
    "Thunder Wave", "Thunder", "Rock Throw", "Earthquake", "Fissure", "Dig",
    "Toxic", "Confusion", "Psychic", "Hypnosis", "Meditate", "Agility",
    "Quick Attack", "Rage", "Teleport", "Night Shade", "Mimic", "Screech",
    "Double Team", "Recover", "Harden", "Minimize", "Smokescreen",
    "Confuse Ray", "Withdraw", "Defense Curl", "Barrier", "Light Screen",
    "Haze", "Reflect", "Focus Energy", "Bide", "Metronome", "Mirror Move",
    "Self-Destruct", "Egg Bomb", "Lick", "Smog", "Sludge", "Bone Club",
    "Fire Blast", "Waterfall", "Clamp", "Swift", "Skull Bash", "Spike Cannon",
    "Constrict", "Amnesia", "Kinesis", "Soft-Boiled", "High Jump Kick",
    "Glare", "Dream Eater", "Poison Gas", "Barrage", "Leech Life",
    "Lovely Kiss", "Sky Attack", "Transform", "Bubble", "Dizzy Punch", "Spore",
    "Flash", "Psywave", "Splash", "Acid Armor", "Crabhammer", "Explosion",
    "Fury Swipes", "Bonemerang", "Rest", "Rock Slide", "Hyper Fang", "Sharpen",
    "Conversion", "Tri Attack", "Super Fang", "Slash", "Substitute",
    "Struggle",
]


# Gen 2 (GSC) held-item names, indexed by item id, from the pokecrystal item
# constants. Unused/glitch ("TERU_SAMA") slots are left blank. TM/HM ids
# (0xBF-0xF7) are filled in programmatically below. Display-only; the held item
# lives at byte 0x01 of each GSC mon struct.
ITEM_NAMES_G2 = {
    0x01: "Master Ball", 0x02: "Ultra Ball", 0x03: "BrightPowder",
    0x04: "Great Ball", 0x05: "Poké Ball", 0x07: "Bicycle", 0x08: "Moon Stone",
    0x09: "Antidote", 0x0A: "Burn Heal", 0x0B: "Ice Heal", 0x0C: "Awakening",
    0x0D: "Parlyz Heal", 0x0E: "Full Restore", 0x0F: "Max Potion",
    0x10: "Hyper Potion", 0x11: "Super Potion", 0x12: "Potion",
    0x13: "Escape Rope", 0x14: "Repel", 0x15: "Max Elixer", 0x16: "Fire Stone",
    0x17: "Thunderstone", 0x18: "Water Stone", 0x1A: "HP Up", 0x1B: "Protein",
    0x1C: "Iron", 0x1D: "Carbos", 0x1E: "Lucky Punch", 0x1F: "Calcium",
    0x20: "Rare Candy", 0x21: "X Accuracy", 0x22: "Leaf Stone",
    0x23: "Metal Powder", 0x24: "Nugget", 0x25: "Poké Doll", 0x26: "Full Heal",
    0x27: "Revive", 0x28: "Max Revive", 0x29: "Guard Spec.", 0x2A: "Super Repel",
    0x2B: "Max Repel", 0x2C: "Dire Hit", 0x2E: "Fresh Water", 0x2F: "Soda Pop",
    0x30: "Lemonade", 0x31: "X Attack", 0x33: "X Defend", 0x34: "X Speed",
    0x35: "X Special", 0x36: "Coin Case", 0x37: "Itemfinder", 0x39: "Exp. Share",
    0x3A: "Old Rod", 0x3B: "Good Rod", 0x3C: "Silver Leaf", 0x3D: "Super Rod",
    0x3E: "PP Up", 0x3F: "Ether", 0x40: "Max Ether", 0x41: "Elixer",
    0x42: "Red Scale", 0x43: "SecretPotion", 0x44: "S.S. Ticket",
    0x45: "Mystery Egg", 0x46: "Clear Bell", 0x47: "Silver Wing",
    0x48: "Moomoo Milk", 0x49: "Quick Claw", 0x4A: "PSNCureBerry",
    0x4B: "Gold Leaf", 0x4C: "Soft Sand", 0x4D: "Sharp Beak",
    0x4E: "PRZCureBerry", 0x4F: "Burnt Berry", 0x50: "Ice Berry",
    0x51: "Poison Barb", 0x52: "King's Rock", 0x53: "Bitter Berry",
    0x54: "Mint Berry", 0x55: "Red Apricorn", 0x56: "TinyMushroom",
    0x57: "Big Mushroom", 0x58: "SilverPowder", 0x59: "Blu Apricorn",
    0x5B: "Amulet Coin", 0x5C: "Ylw Apricorn", 0x5D: "Grn Apricorn",
    0x5E: "Cleanse Tag", 0x5F: "Mystic Water", 0x60: "TwistedSpoon",
    0x61: "Wht Apricorn", 0x62: "Blackbelt", 0x63: "Blk Apricorn",
    0x65: "Pnk Apricorn", 0x66: "BlackGlasses", 0x67: "Slowpoketail",
    0x68: "Pink Bow", 0x69: "Stick", 0x6A: "Smoke Ball", 0x6B: "NeverMeltIce",
    0x6C: "Magnet", 0x6D: "MiracleBerry", 0x6E: "Pearl", 0x6F: "Big Pearl",
    0x70: "Everstone", 0x71: "Spell Tag", 0x72: "RageCandyBar", 0x73: "GS Ball",
    0x74: "Blue Card", 0x75: "Miracle Seed", 0x76: "Thick Club",
    0x77: "Focus Band", 0x79: "EnergyPowder", 0x7A: "Energy Root",
    0x7B: "Heal Powder", 0x7C: "Revival Herb", 0x7D: "Hard Stone",
    0x7E: "Lucky Egg", 0x7F: "Card Key", 0x80: "Machine Part",
    0x81: "Egg Ticket", 0x82: "Lost Item", 0x83: "Stardust", 0x84: "Star Piece",
    0x85: "Basement Key", 0x86: "Pass", 0x8A: "Charcoal", 0x8B: "Berry Juice",
    0x8C: "Scope Lens", 0x8F: "Metal Coat", 0x90: "Dragon Fang",
    0x92: "Leftovers", 0x96: "MysteryBerry", 0x97: "Dragon Scale",
    0x98: "Berserk Gene", 0x9C: "Sacred Ash", 0x9D: "Heavy Ball",
    0x9E: "Flower Mail", 0x9F: "Level Ball", 0xA0: "Lure Ball", 0xA1: "Fast Ball",
    0xA3: "Light Ball", 0xA4: "Friend Ball", 0xA5: "Moon Ball", 0xA6: "Love Ball",
    0xA7: "Normal Box", 0xA8: "Gorgeous Box", 0xA9: "Sun Stone",
    0xAA: "Polkadot Bow", 0xAC: "Up-Grade", 0xAD: "Berry", 0xAE: "Gold Berry",
    0xAF: "SquirtBottle", 0xB1: "Park Ball", 0xB2: "Rainbow Wing",
    0xB4: "Brick Piece", 0xB5: "Surf Mail", 0xB6: "LiteBlueMail",
    0xB7: "PortraitMail", 0xB8: "Lovely Mail", 0xB9: "Eon Mail",
    0xBA: "Morph Mail", 0xBB: "BlueSky Mail", 0xBC: "Music Mail",
    0xBD: "Mirage Mail",
}
for _tm in range(50):       # TM01..TM50 -> 0xBF..0xF0
    ITEM_NAMES_G2[0xBF + _tm] = f"TM{_tm + 1:02d}"
for _hm in range(7):        # HM01..HM07 -> 0xF1..0xF7
    ITEM_NAMES_G2[0xF1 + _hm] = f"HM{_hm + 1:02d}"


# Gen 2 gender ratio by national dex (idx 0 unused), from veekun/PokeAPI
# `gender_rate`: -1 = genderless, else the female share in eighths (0 = always
# male, 8 = always female). A Pokémon is female iff Attack DV < 2*rate. Gen 1 had
# no gender, so this is Gen 2 only. The species byte in a GSC record is the dex #.
GENDER_RATE_G2 = [
    -1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8,
    0, 0, 0, 6, 6, 6, 6, 6, 6, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4, 2,
    2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, -1, -1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, -1, -1, 4, 4, 4, 4, 0, 0, 4, 4, 4, 4,
    4, 8, 4, 8, 4, 4, 4, 4, -1, -1, 4, 4, 8, 2, 2, 4,
    0, 4, 4, 4, -1, 1, 1, 1, 1, -1, 1, 1, 1, 1, 1, 1,
    -1, -1, -1, 4, 4, 4, -1, -1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 1,
    1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 1, 1, 4, 4, 4, -1, 4, 4, 4, 4, 4, 4,
    4, 6, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, -1, 4, 4, 0, 0, 8, 2,
    2, 8, 8, -1, -1, -1, 4, 4, 4, -1, -1, -1,
]


# Gen 1 type ids -> name (sparse; ids up to 26).
TYPE_NAMES = {
    0: "Normal", 1: "Fighting", 2: "Flying", 3: "Poison", 4: "Ground",
    5: "Rock", 7: "Bug", 8: "Ghost", 20: "Fire", 21: "Water", 22: "Grass",
    23: "Electric", 24: "Psychic", 25: "Ice", 26: "Dragon",
}

# Gen 2-native species (national dex 152-251) types, as they were in Gen 2 (Dark
# and Steel exist; Fairy does not). Gen 1 species' types are read from the
# engine's authoritative rby/types.bin and matched by name.
GEN2_NATIVE_TYPES = {
    152: ["Grass"], 153: ["Grass"], 154: ["Grass"], 155: ["Fire"], 156: ["Fire"],
    157: ["Fire"], 158: ["Water"], 159: ["Water"], 160: ["Water"], 161: ["Normal"],
    162: ["Normal"], 163: ["Normal", "Flying"], 164: ["Normal", "Flying"],
    165: ["Bug", "Flying"], 166: ["Bug", "Flying"], 167: ["Bug", "Poison"],
    168: ["Bug", "Poison"], 169: ["Poison", "Flying"], 170: ["Water", "Electric"],
    171: ["Water", "Electric"], 172: ["Electric"], 173: ["Normal"], 174: ["Normal"],
    175: ["Normal"], 176: ["Normal", "Flying"], 177: ["Psychic", "Flying"],
    178: ["Psychic", "Flying"], 179: ["Electric"], 180: ["Electric"], 181: ["Electric"],
    182: ["Grass"], 183: ["Water"], 184: ["Water"], 185: ["Rock"], 186: ["Water"],
    187: ["Grass", "Flying"], 188: ["Grass", "Flying"], 189: ["Grass", "Flying"],
    190: ["Normal"], 191: ["Grass"], 192: ["Grass"], 193: ["Bug", "Flying"],
    194: ["Water", "Ground"], 195: ["Water", "Ground"], 196: ["Psychic"], 197: ["Dark"],
    198: ["Dark", "Flying"], 199: ["Water", "Psychic"], 200: ["Ghost"], 201: ["Psychic"],
    202: ["Psychic"], 203: ["Normal", "Psychic"], 204: ["Bug"], 205: ["Bug", "Steel"],
    206: ["Normal"], 207: ["Ground", "Flying"], 208: ["Steel", "Ground"], 209: ["Normal"],
    210: ["Normal"], 211: ["Water", "Poison"], 212: ["Bug", "Steel"], 213: ["Bug", "Rock"],
    214: ["Bug", "Fighting"], 215: ["Dark", "Ice"], 216: ["Normal"], 217: ["Normal"],
    218: ["Fire"], 219: ["Fire", "Rock"], 220: ["Ice", "Ground"], 221: ["Ice", "Ground"],
    222: ["Water", "Rock"], 223: ["Water"], 224: ["Water"], 225: ["Ice", "Flying"],
    226: ["Water", "Flying"], 227: ["Steel", "Flying"], 228: ["Dark", "Fire"],
    229: ["Dark", "Fire"], 230: ["Water", "Dragon"], 231: ["Ground"], 232: ["Ground"],
    233: ["Normal"], 234: ["Normal"], 235: ["Normal"], 236: ["Fighting"],
    237: ["Fighting"], 238: ["Ice", "Psychic"], 239: ["Electric"], 240: ["Fire"],
    241: ["Normal"], 242: ["Normal"], 243: ["Electric"], 244: ["Fire"], 245: ["Water"],
    246: ["Rock", "Ground"], 247: ["Rock", "Ground"], 248: ["Rock", "Dark"],
    249: ["Psychic", "Flying"], 250: ["Fire", "Flying"], 251: ["Psychic", "Grass"],
}


def _types_by_internal_index():
    """Gen 1 species (internal index) -> [type names], from rby/types.bin."""
    data = open(os.path.join(ENGINE_DATA, "rby", "types.bin"), "rb").read()
    out = []
    for i in range(256):
        names = []
        for t in (data[i * 2], data[i * 2 + 1]):
            n = TYPE_NAMES.get(t, "")
            if n and n not in names:
                names.append(n)
        out.append(names)
    return out


def _js_array(name, items):
    body = ",\n  ".join(
        ", ".join(_q(x) for x in items[i : i + 8]) for i in range(0, len(items), 8)
    )
    return f"const {name} = [\n  {body}\n];"


def _q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def _load_names(path):
    with open(path, "r", encoding="utf-8") as fh:
        names = [ln.rstrip("\n") for ln in fh]
    while len(names) < 256:
        names.append(f"#{len(names)}")
    return names


def main():
    names = _load_names(ENGINE_NAMES)        # Gen 1 (internal index order)
    names2 = _load_names(GEN2_NAMES)          # Gen 2 (dex order)
    types = [TYPE_NAMES.get(i, "") for i in range(64)]

    # Species -> types tables. Gen 1: by internal index (the captured species
    # byte), from game data. Gen 2: by national dex; Gen 1 species matched by
    # name, Gen 2 natives from the table above.
    types_g1 = _types_by_internal_index()
    g1_by_name = {names[i]: types_g1[i] for i in range(256)
                  if names[i] and not names[i].startswith("#") and names[i] != "MissingNo."}
    types_g2 = [[] for _ in range(256)]
    for dex in range(1, 252):
        if dex in GEN2_NATIVE_TYPES:
            types_g2[dex] = GEN2_NATIVE_TYPES[dex]
        elif dex < len(names2):
            types_g2[dex] = g1_by_name.get(names2[dex], [])

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    item_names_g2 = [ITEM_NAMES_G2.get(i, "") for i in range(256)]
    gender_rate_g2 = [(GENDER_RATE_G2[i] if i < len(GENDER_RATE_G2) else -1)
                      for i in range(256)]
    exported = ["SPECIES_NAMES", "SPECIES_NAMES_G2", "MOVE_NAMES", "TYPE_NAMES",
                "TYPES_G1", "TYPES_G2", "ITEM_NAMES_G2", "GENDER_RATE_G2"]
    footer = (
        "\n// Expose for the browser (other <script>s) and for Node tests.\n"
        "if (typeof globalThis !== 'undefined') {\n"
        + "".join(f"  globalThis.{n} = {n};\n" for n in exported)
        + "}\n"
        f"if (typeof module !== 'undefined') module.exports = {{ {', '.join(exported)} }};\n"
    )

    def _js_nested(name, rows):
        body = ",\n  ".join("[" + ", ".join(_q(x) for x in row) + "]" for row in rows)
        return f"const {name} = [\n  {body}\n];"

    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("// AUTO-GENERATED by tools/gen_webui_data.py -- do not edit.\n")
        fh.write(_js_array("SPECIES_NAMES", names) + "\n\n")
        fh.write(_js_array("SPECIES_NAMES_G2", names2) + "\n\n")
        fh.write(_js_array("MOVE_NAMES", MOVE_NAMES) + "\n\n")
        fh.write(_js_array("TYPE_NAMES", types) + "\n\n")
        fh.write(_js_nested("TYPES_G1", types_g1) + "\n\n")
        fh.write(_js_nested("TYPES_G2", types_g2) + "\n\n")
        fh.write(_js_array("ITEM_NAMES_G2", item_names_g2) + "\n\n")
        nums = ",\n  ".join(", ".join(str(x) for x in gender_rate_g2[i:i + 16])
                            for i in range(0, len(gender_rate_g2), 16))
        fh.write(f"const GENDER_RATE_G2 = [\n  {nums}\n];\n")
        fh.write(footer)
    print(f"Wrote {OUT}  (Gen1 {len(names)} / Gen2 {len(names2)} species, "
          f"{len(MOVE_NAMES)} moves; types G1+G2 baked)")


if __name__ == "__main__":
    main()
