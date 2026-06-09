// Build a .sav from a hex record using the browser's decode.js buildSav().
//   node tools/build_sav.js <rec.hex> <out.sav>
const path = require("path");
const fs = require("fs");
require(path.join(__dirname, "..", "webui", "pokedata.js"));
const { buildSav, hexToBytes } = require(path.join(__dirname, "..", "webui", "decode.js"));

const hex = fs.readFileSync(process.argv[2], "utf8").trim();
const sav = buildSav(hexToBytes(hex));
fs.writeFileSync(process.argv[3], Buffer.from(sav));
console.log(`wrote ${process.argv[3]} (${sav.length} bytes)`);
