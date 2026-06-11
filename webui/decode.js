// Gen 1 (RBY) + Gen 2 (GSC) trade-record decoder. Shared by the WebUI and the
// Node test. Depends on globalThis.SPECIES_NAMES / SPECIES_NAMES_G2 / MOVE_NAMES
// / TYPE_NAMES (from pokedata.js). The generation is inferred from record length
// (625 = Gen 1, 1036 = Gen 2).
"use strict";

const REC_LEN = 625;  // minimum (Gen 1); Gen 2 records are 1036

// Per-generation layout. Offsets within the party section (sec1) and within each
// 44/48-byte Pokémon struct.
const GEN1 = {
  gen: 1, namesKey: "SPECIES_NAMES", typesKey: "TYPES_G1", spriteSet: "red-blue",
  sec1: [10, 418], sec2: [428, 197],
  countPos: 0x0B, monPos: 0x13, monLen: 0x2C, otPos: 0x11B, nickPos: 0x15D, nameLen: 0x0B,
  curHp: 1, maxHp: 0x22, moves: 8, pp: 0x1D, otId: 0x0C, level: 0x21, dv: 0x1B,
  stats: [["HP", 0x22], ["ATK", 0x24], ["DEF", 0x26], ["SPD", 0x28], ["SPC", 0x2A]],
};
const GEN2 = {
  gen: 2, namesKey: "SPECIES_NAMES_G2", typesKey: "TYPES_G2", spriteSet: "crystal",
  sec1: [10, 444], sec2: [454, 197],
  countPos: 0x0B, monPos: 0x15, monLen: 0x30, otPos: 0x135, nickPos: 0x177, nameLen: 0x0B,
  itemPos: 1, curHp: 0x22, maxHp: 0x24, moves: 2, pp: 0x17, otId: 6, level: 0x1F, dv: 0x15,
  stats: [["HP", 0x24], ["ATK", 0x26], ["DEF", 0x28], ["SPD", 0x2A], ["SpA", 0x2C], ["SpD", 0x2E]],
};
function layoutFor(len) { return len >= 1036 ? GEN2 : GEN1; }

// Species (by name) that evolve when traded (Gen 1 + the always-trade Gen 2 ones).
const TRADE_EVOLVERS = new Set(["Kadabra", "Machoke", "Graveler", "Haunter"]);

function u16(b, o) { return (b[o] << 8) | b[o + 1]; }

// RBY/GSC party patch list: base 0x13, start 7, two sets (same for both gens).
function applyPatches(data, patch) {
  let num = 2, base = 0x13, start = 7, i = 0;
  while (num > 0 && start + i < patch.length) {
    const readPos = patch[start + i]; i++;
    if (readPos === 0xFF) { num--; base += 0xFC; }
    else if (readPos > 0 && readPos + base < data.length) data[readPos + base - 1] = 0xFE;
  }
}

// Gen 1/2 English text -> string.
function gbText(bytes) {
  let s = "";
  for (const v of bytes) {
    if (v === 0x50 || v === 0x00) break;
    if (v === 0x7F) { s += " "; continue; }
    if (v >= 0x80 && v <= 0x99) s += String.fromCharCode(65 + v - 0x80);
    else if (v >= 0xA0 && v <= 0xB9) s += String.fromCharCode(97 + v - 0xA0);
    else if (v >= 0xF6 && v <= 0xFF) s += String.fromCharCode(48 + v - 0xF6);
    else if (v === 0xEF) s += "♂";
    else if (v === 0xF5) s += "♀";
    else s += "?";
  }
  return s;
}

// Decode one Pokémon from its struct (m) + OT-name + nickname byte arrays,
// using the given generation layout L.
function decodeMon(m, otB, nickB, L) {
  const names = globalThis[L.namesKey] || [];
  const moveNames = globalThis.MOVE_NAMES || [];
  const species = m[0];
  const name = names[species] || ("#" + species);
  const nickname = gbText(nickB);

  const moves = [0, 1, 2, 3].map(k => m[L.moves + k]);
  const pps = [0, 1, 2, 3].map(k => m[L.pp + k] & 0x3F);

  const d1 = m[L.dv], d2 = m[L.dv + 1];
  const dv = { atk: d1 >> 4, def: d1 & 0xF, spd: d2 >> 4, spc: d2 & 0xF };
  dv.hp = ((dv.atk & 1) << 3) | ((dv.def & 1) << 2) | ((dv.spd & 1) << 1) | (dv.spc & 1);

  const types = ((globalThis[L.typesKey] || [])[species] || []).slice();

  // Held item (Gen 2 only; Gen 1 has no held-item byte). byte 0x01 of the struct.
  let heldItem = 0, heldItemName = "";
  if (L.itemPos !== undefined) {
    heldItem = m[L.itemPos];
    if (heldItem) heldItemName = (globalThis.ITEM_NAMES_G2 || [])[heldItem] || ("#" + heldItem);
  }

  // Shiny + gender are derived from DVs, and only exist in Gen 2 (Gen 1 had
  // neither). Shiny: Def/Spd/Spc DV all 10 and Atk DV in {2,3,6,7,10,11,14,15}.
  // Gender: female iff Atk DV < 2*rate (rate from GENDER_RATE_G2; -1 genderless).
  let shiny = false, gender = null;
  if (L.gen === 2) {
    shiny = dv.def === 10 && dv.spd === 10 && dv.spc === 10 && (dv.atk & 2) !== 0;
    const rate = (globalThis.GENDER_RATE_G2 || [])[species];
    if (rate !== undefined && rate >= 0) gender = dv.atk < 2 * rate ? "F" : "M";
  }

  const raw = new Uint8Array(L.monLen + 2 * L.nameLen);
  raw.set(m.subarray(0, L.monLen), 0);
  raw.set(otB.subarray(0, L.nameLen), L.monLen);
  raw.set(nickB.subarray(0, L.nameLen), L.monLen + L.nameLen);

  return {
    gen: L.gen, spriteSet: L.spriteSet, species, name, nickname,
    otName: gbText(otB),
    otId: u16(m, L.otId),
    level: m[L.level],
    heldItem, heldItemName, shiny, gender,
    hp: u16(m, L.curHp),
    maxHp: u16(m, L.maxHp),
    types,
    stats: L.stats.map(([label, off]) => ({ label, val: u16(m, off) })),
    dv,
    nicknamed: nickname.toUpperCase() !== name.toUpperCase(),
    tradeEvolves: TRADE_EVOLVERS.has(name),
    moves: moves.map((mv, k) => ({ id: mv, name: moveNames[mv] || ("#" + mv), pp: pps[k] }))
                .filter(x => x.id !== 0),
    raw,
  };
}

// Decode a whole captured party block (625 = Gen 1, 1036 = Gen 2).
function decodeRecord(bytes) {
  const L = layoutFor(bytes.length);
  const sec1 = bytes.slice(L.sec1[0], L.sec1[0] + L.sec1[1]);
  const sec2 = bytes.slice(L.sec2[0], L.sec2[0] + L.sec2[1]);
  applyPatches(sec1, sec2);

  let count = sec1[L.countPos];
  if (count < 1 || count > 6) count = 0;
  const mons = [];
  for (let i = 0; i < count; i++) {
    mons.push(decodeMon(
      sec1.subarray(L.monPos + i * L.monLen, L.monPos + (i + 1) * L.monLen),
      sec1.subarray(L.otPos + i * L.nameLen, L.otPos + (i + 1) * L.nameLen),
      sec1.subarray(L.nickPos + i * L.nameLen, L.nickPos + (i + 1) * L.nameLen),
      L));
  }
  return mons;
}

// Gen 2 Mail items (gsc/ids_mail.bin): Flower Mail + the 0xB5..0xBD mails.
function isMailItem(it) { return it === 0x9E || (it >= 0xB5 && it <= 0xBD); }

// Decode a single dex entry: struct + OT name + nickname (+ mail + sender when a
// Gen 2 mon holds Mail), for the given gen.
function decodeMonRecord(bytes, gen) {
  const L = gen === 2 ? GEN2 : GEN1;
  const m = bytes.subarray(0, L.monLen);
  const otB = bytes.subarray(L.monLen, L.monLen + L.nameLen);
  const nickB = bytes.subarray(L.monLen + L.nameLen, L.monLen + 2 * L.nameLen);
  const mon = decodeMon(m, otB, nickB, L);

  // Mail: appended after nick (0x21 message + 0x0E sender) when item is Mail.
  const base = L.monLen + 2 * L.nameLen, MAIL = 0x21, SENDER = 0x0E;
  if (gen === 2 && bytes.length >= base + MAIL + SENDER && isMailItem(mon.heldItem)) {
    mon.mail = gbText(bytes.subarray(base, base + MAIL));
    mon.mailFrom = gbText(bytes.subarray(base + MAIL, base + MAIL + SENDER));
  }
  return mon;
}

// Sprite slug for Pokémon DB Red/Blue sprites.
function spriteSlug(name) {
  return name.toLowerCase()
    .replace(/♀/g, "-f").replace(/♂/g, "-m")
    .replace(/['’.]/g, "").replace(/\s+/g, "-");
}

function hexToBytes(hex) {
  const a = new Uint8Array(hex.length / 2);
  for (let i = 0; i < a.length; i++) a[i] = parseInt(hex.substr(i * 2, 2), 16);
  return a;
}

// Build a 32 KB Gen 1 (.sav) from a Gen 1 full-party record by slicing the
// (save-identical) party block out of the trade block. Gen 2 records route
// through buildSavFromMons (their save layout is rebuilt, not sliced).
function buildSav(record) {
  if (record.length >= 1036) return buildSavFromMons(decodeRecord(record), 2);
  if (record.length !== 625) return null;
  const OT_OFF = 0x2598, PARTY_OFF = 0x2F2C, BOX_OFF = 0x30C0, CK_OFF = 0x3523, CK_START = 0x2598;
  const sec1 = record.slice(10, 10 + 418);
  const sec2 = record.slice(428, 428 + 197);
  applyPatches(sec1, sec2);
  const block = sec1.slice(0x0B, 0x0B + 404);
  const ot = sec1.slice(0x11B, 0x11B + 11);

  const sav = new Uint8Array(0x8000);
  sav.set(block, PARTY_OFF);
  sav.set(ot, OT_OFF);
  sav[BOX_OFF] = 0; sav[BOX_OFF + 1] = 0xFF;
  let s = 0;
  for (let i = CK_START; i < CK_OFF; i++) s = (s + sav[i]) & 0xFF;
  sav[CK_OFF] = (~s) & 0xFF;
  return sav;
}

// Save layout per generation/game. Gen 1 uses an 8-bit ones-complement checksum;
// Gen 2 (GSC) uses a 16-bit byte sum written to two positions plus a backup copy
// (offsets from PKHeX SAV2Offsets). Each mon's .raw is struct + OT(11) + nick(11).
const SAV_LAYOUTS = {
  1: {
    monLen: 0x2C, partyOff: 0x2F2C, otOff: 0x2598, boxOff: 0x30C0,
    ck: { pos: 0x3523, start: 0x2598, end: 0x3522, bits: 8 },
  },
  2: {  // Crystal (US/International)
    monLen: 0x30, partyOff: 0x2865, otOff: 0x200B, boxOff: 0x2D10,
    ck: { start: 0x2009, end: 0x2B82, pos: 0x2D0D, pos2: 0x1F0D, bits: 16 },
    backups: [[0x2009, 0xB7A, 0x1209]],
  },
};

// Build a 32 KB .sav from decoded per-mon records (1-6 of one generation).
function buildSavFromMons(mons, gen) {
  const L = SAV_LAYOUTS[gen];
  if (!L || mons.length < 1 || mons.length > 6) return null;
  const ML = L.monLen, NL = 11, count = mons.length;

  // Party block: count + 6 species + 0xFF terminator + 6 mons + 6 OT + 6 nick.
  const block = new Uint8Array(1 + 7 + 6 * ML + 6 * NL + 6 * NL);
  block[0] = count;
  const monBase = 8, otBase = 8 + 6 * ML, nickBase = otBase + 6 * NL;
  mons.forEach((m, i) => {
    block[1 + i] = m.species;
    block.set(m.raw.subarray(0, ML), monBase + i * ML);
    block.set(m.raw.subarray(ML, ML + NL), otBase + i * NL);
    block.set(m.raw.subarray(ML + NL, ML + 2 * NL), nickBase + i * NL);
  });
  block[1 + count] = 0xFF;

  const sav = new Uint8Array(0x8000);
  sav.set(block, L.partyOff);
  sav.set(mons[0].raw.subarray(ML, ML + NL), L.otOff);  // OT name = first mon's
  if (L.boxOff !== undefined) { sav[L.boxOff] = 0; sav[L.boxOff + 1] = 0xFF; }

  let s = 0;
  for (let i = L.ck.start; i <= L.ck.end; i++) s += sav[i];
  if (L.ck.bits === 8) {
    sav[L.ck.pos] = (~s) & 0xFF;
  } else {
    s &= 0xFFFF;
    for (const pos of [L.ck.pos, L.ck.pos2]) { sav[pos] = s & 0xFF; sav[pos + 1] = (s >> 8) & 0xFF; }
    for (const [src, len, dst] of (L.backups || [])) sav.copyWithin(dst, src, src + len);
  }
  return sav;
}

const _exports = { decodeRecord, decodeMonRecord, decodeMon, spriteSlug, hexToBytes, gbText, applyPatches, buildSav, buildSavFromMons, layoutFor, REC_LEN };
if (typeof globalThis !== "undefined") Object.assign(globalThis, _exports);
if (typeof module !== "undefined") module.exports = _exports;
