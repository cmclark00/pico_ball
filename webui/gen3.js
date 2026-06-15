// Gen 3 (R/S/E/FR/LG) glue for the WebUI. Unlike Gen 1/2 (which the on-device
// vault firmware captures and we read over Web Serial), Gen 3 is driven entirely
// from the browser over WebUSB against the reconfigurable firmware, mirroring
// host/extract.py --gen 3:
//   1. open the board (WebUSB) and configure() the 4-byte SIO32 link,
//   2. multiboot the Gen3-to-GenX homebrew into the GBA's RAM,
//   3. run the Gen 3 trade-partner protocol to capture/inject (milestone 2).
//
// Exposed on window.PicoGen3 so the inline app script can call it. Decode/render
// reuses the global decodeMonRecord + pokedata_gen3.js already in index.html.
import { UsbLink } from "./usb_link.js";
import { multiboot } from "./multiboot.js";
import { captureGen3Party } from "./gen3_trade.js";

// Where the ~248 KB multiboot image might be reachable from, depending on how
// the page is served. third_party/ is gitignored and not redistributed, so we
// fall back to a file picker if none of these resolve.
const GBA_CANDIDATES = [
  "./pokemon_gen3_to_genx_mb.gba",
  "../third_party/gen3/pokemon_gen3_to_genx_mb.gba",
  "third_party/gen3/pokemon_gen3_to_genx_mb.gba",
];

async function loadMultibootImage(log) {
  for (const url of GBA_CANDIDATES) {
    try {
      const res = await fetch(url);
      if (res.ok) {
        const buf = new Uint8Array(await res.arrayBuffer());
        if (buf.length > 0x1000) { log(`Loaded multiboot image (${url})`); return buf; }
      }
    } catch (e) { /* try next */ }
  }
  // Fall back: ask the user to pick the .gba.
  log("Could not auto-locate pokemon_gen3_to_genx_mb.gba — pick it manually.");
  return await pickFile();
}

function pickFile() {
  return new Promise((resolve, reject) => {
    const inp = document.createElement("input");
    inp.type = "file";
    inp.accept = ".gba";
    inp.onchange = async () => {
      const f = inp.files && inp.files[0];
      if (!f) return reject(new Error("No file chosen"));
      resolve(new Uint8Array(await f.arrayBuffer()));
    };
    inp.click();
  });
}

// Open WebUSB, verify SIO32 firmware, and multiboot the GBA. Returns the open
// UsbLink (caller closes it). Throws on any failure with a user-facing message.
async function bootGen3(rawLog, onProgress) {
  const log = (m) => { console.log("[gen3]", m); rawLog(m); };
  const link = new UsbLink();
  await link.open();                       // must be inside a user gesture
  console.log("UsbLink open; connect request type =", link._connectType || "(none accepted)");
  log("Board opened. Configuring 4-byte SIO32 link…");
  const ok = await link.configure(4);
  console.log("configure(4) ->", ok);
  if (!ok) {
    await link.close();
    throw new Error(
      "The board didn't acknowledge SIO32 config (no ack byte). Is the " +
      "reconfigurable firmware flashed (gbusb_reconfigurable.uf2)? Check console.");
  }
  const gba = await loadMultibootImage(log);
  log("Make sure the GBA is ON at the BIOS/boot screen (no game running), " +
      "cartridge inserted, then continue.");
  await multiboot(link, gba, log, onProgress);
  return link;
}

// Public entry: full Gen 3 capture over WebUSB — multiboot the homebrew, wait for
// the user to reach its trade screen, then run the trade-partner exchange and
// return the captured party as an array of 100-byte .pk3 records.
async function capture(rawLog, onProgress) {
  const log = (m) => { console.log("[gen3]", m); rawLog(m); };
  const link = await bootGen3(rawLog, onProgress);
  try {
    const ok = window.confirm(
      "Multiboot complete! 🎉\n\n" +
      "On the GBA: open Gen3-to-GenX, choose the Gen 3 trade option, and advance " +
      "until it is waiting to trade (the cartridge must be inserted).\n\n" +
      "Click OK to capture your party.");
    if (!ok) { log("Cancelled before capture."); return []; }
    log("Capturing party from the cartridge…");
    const mons = await captureGen3Party(link, log);
    log(`Captured ${mons.length} Pokémon.`);
    return mons;
  } finally {
    await link.close();
  }
}

window.PicoGen3 = { capture, bootGen3 };
