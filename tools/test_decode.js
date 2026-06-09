// Validates webui/decode.js against a real captured record. Run via
// tools/test_webui.sh (which first rebuilds the record from the vault).
//   argv[2] = hex record file
//   argv[3] = expected "Name:lvl,..." (optional)
//   argv[4] = vault dir of original .pk1 files to byte-compare against (optional)
const path = require("path");
const fs = require("fs");

require(path.join(__dirname, "..", "webui", "pokedata.js")); // sets globalThis tables
const { decodeRecord, hexToBytes } = require(path.join(__dirname, "..", "webui", "decode.js"));

const hex = fs.readFileSync(process.argv[2] || "/tmp/rec.hex", "utf8").trim();
const mons = decodeRecord(hexToBytes(hex));

console.log(JSON.stringify(
  mons.map(m => ({
    name: m.name, level: m.level, types: m.types, nick: m.nickname, ot: m.otName,
    hp: `${m.hp}/${m.maxHp}`, stats: m.stats, dv: m.dv,
    flags: [m.nicknamed ? "nicknamed" : "", m.tradeEvolves ? "trade-evolves" : ""].filter(Boolean),
    moves: m.moves.map(x => `${x.name}(${x.pp})`),
  })), null, 2));

let failed = false;

const expect = process.argv[3];
if (expect) {
  const got = mons.map(m => `${m.name}:${m.level}`).join(",");
  if (got !== expect) { console.error("MISMATCH names/levels\n got: " + got + "\n exp: " + expect); failed = true; }
}

// Byte-compare each exported .pk1 against the original vault file.
const vaultDir = process.argv[4];
if (vaultDir) {
  const files = fs.readdirSync(vaultDir).filter(f => /^party0.*\.pk1$/.test(f)).sort();
  mons.forEach((m, i) => {
    const orig = new Uint8Array(fs.readFileSync(path.join(vaultDir, files[i])));
    const same = orig.length === m.raw.length && orig.every((b, k) => b === m.raw[k]);
    console.log(`  pk1[${i}] ${files[i]}: exported bytes ${same ? "MATCH" : "DIFFER"} (${m.raw.length}B)`);
    if (!same) failed = true;
  });
}

if (failed) process.exit(1);
console.log("PASS");
