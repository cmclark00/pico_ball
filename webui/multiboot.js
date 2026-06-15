// Multiboot a GBA program (Gen3-to-GenX) into the console's RAM over the link
// cable. Direct port of third_party/PokemonGB_Online_Trades/multiboot.py
// (itself from Squaresweets/TileWorldGBA). Drives the reconfigurable firmware
// via a UsbLink (see usb_link.js).
//
// The GBA BIOS multiboot protocol: handshake, then stream the image XOR-scrambled
// with a per-word LCG seeded by a value the BIOS returns, then a final CRC.
// 32-bit math overflows JS Numbers on the LCG multiply, so the seed/CRC use
// BigInt; per-word XORs stay as 32-bit Number ops with >>> 0.

const MAX_PACKET = 0x40;

function configureList(usBetween, bytesForTransfer) {
  const cfg = [];
  for (let i = 0; i < 8; i++) cfg.push(0xCA, 0xFE);
  for (let i = 0; i < 4; i++) cfg.push(0xDE, 0xAD, 0xBE, 0xEF);
  cfg.push(usBetween & 0xFF, (usBetween >> 8) & 0xFF,
           (usBetween >> 16) & 0xFF, bytesForTransfer & 0xFF);
  return cfg;
}

// link: a UsbLink. gbaBytes: Uint8Array of the .gba. onProgress(frac): optional.
export async function multiboot(link, gbaBytes, log = () => {}, onProgress = () => {}) {
  const sender = (val, n) => link.sendByte(val, n);
  const listSender = (data, chunk) => link.sendList(data, chunk);
  // Read one logical response word (fragment-safe, real timeout). The clears that
  // multiboot.py does via read_all become link.drain() (no giant BigInt over the
  // ~248KB of buffered IN responses the pump collects during the data send).
  const readWord = () => link.readAllValue();

  log("Preparing data…");
  let fsize = gbaBytes.length;
  // Pad with 64 zero bytes (avoids reads past the end during scrambling).
  const content = new Uint8Array(fsize + 64);
  content.set(gbaBytes);

  if (fsize > 0x3FF40) throw new Error(`File too large, max ${0x3FF40} bytes`);
  fsize = (fsize + 0xF) & ~0xF;

  const nWords = (fsize - 0xC0) >> 2;
  const sendingData = new Uint32Array(nWords);
  let crcC = 0xC387;
  for (let i = 0xC0; i < fsize; i += 4) {
    let dat = (content[i] | (content[i + 1] << 8) |
               (content[i + 2] << 16) | (content[i + 3] << 24)) >>> 0;
    let tmp = dat;
    for (let b = 0; b < 32; b++) {
      const bit = (crcC ^ tmp) & 1;
      crcC = bit === 0 ? (crcC >> 1) : ((crcC >> 1) ^ 0xC37B);
      tmp = tmp >>> 1;
    }
    dat = (dat ^ (((0xFE000000 - i) >>> 0)) ^ 0x43202F2F) >>> 0;
    sendingData[(i - 0xC0) >> 2] = dat;
  }
  log("Data preloaded…");

  await link.drain();
  await listSender(configureList(36, 4), configureList(36, 4).length);
  await link.drain();

  let recv;
  let tries = 0;
  for (;;) {
    await sender(0x6202, 4);
    recv = await readWord();
    if ((recv >> 16n) === 0x7202n) break;
    if (++tries % 50 === 0) log(`Waiting for GBA multiboot handshake… (got 0x${recv.toString(16)})`);
    if (tries > 600) throw new Error(
      `GBA never answered the multiboot handshake (last reply 0x${recv.toString(16)}). ` +
      `Power the GBA off/on, leave it on the logo screen (no game), check the link cable.`);
  }
  log("Handshake started…");
  await sender(0x6102, 4);

  // Send the 0xC0-byte header (96 halfwords).
  for (let i = 0; i < 96; i++) {
    const out = content[i * 2] + (content[i * 2 + 1] << 8);
    await sender(out, 4);
  }

  await sender(0x6200, 4);
  await sender(0x6200, 4);
  await sender(0x63D1, 4);
  await link.drain();                 // clear buffer

  await sender(0x63D1, 4);
  let token = await readWord();
  if (((token >> 24n) & 0xFFn) !== 0x73n) throw new Error("Failed handshake!");
  log("Handshake successful!");

  let crcA = Number((token >> 16n) & 0xFFn);
  let seed = (0xFFFF00D1n | (BigInt(crcA) << 8n)) & 0xFFFFFFFFn;
  crcA = (crcA + 0xF) & 0xFF;

  await sender(0x6400 | crcA, 4);
  await link.drain();

  await sender((fsize - 0x190) >> 2, 4);
  token = await readWord();
  const crcB = Number((token >> 16n) & 0xFFn);
  log(`Sending ${fsize} bytes…`);

  const complete = new Uint8Array(nWords * 4);
  for (let i = 0; i < nWords; i++) {
    seed = (seed * 0x6F646573n + 1n) & 0xFFFFFFFFn;
    const x = (BigInt(sendingData[i]) ^ seed) & 0xFFFFFFFFn;
    complete[i * 4]     = Number((x >> 24n) & 0xFFn);
    complete[i * 4 + 1] = Number((x >> 16n) & 0xFFn);
    complete[i * 4 + 2] = Number((x >> 8n) & 0xFFn);
    complete[i * 4 + 3] = Number(x & 0xFFn);
  }

  // Stream the scrambled image in large blocks (one bulk OUT each, WebUSB
  // packetizes into 64-byte wire packets under USB flow control). Fewer JS
  // round-trips than 64-byte chunks; correctness is unaffected either way.
  const BLOCK = 16384;       // 256 wire packets per transferOut
  const t0 = performance.now();
  for (let i = 0; i < complete.length; i += BLOCK) {
    await link.sendList(complete.subarray(i, i + BLOCK), BLOCK);
    onProgress(Math.min(1, (i + BLOCK) / complete.length));
  }
  const dt = (performance.now() - t0) / 1000;
  console.log(`[gen3] data send: ${complete.length} bytes / ${nWords} words in ` +
    `${dt.toFixed(2)}s (${Math.round(nWords / dt)} words/s; firmware paces ~36µs/word ` +
    `=> expect >= ~2.3s)`);
  log(`Data sent (${dt.toFixed(1)}s)`);

  // Fold the final CRC and confirm.
  let tmp = (0xFFFF0000 | (crcB << 8) | crcA) >>> 0;
  for (let b = 0; b < 32; b++) {
    const bit = (crcC ^ tmp) & 1;
    crcC = bit === 0 ? (crcC >> 1) : ((crcC >> 1) ^ 0xC37B);
    tmp = tmp >>> 1;
  }

  await link.drain();
  await sender(0x0065, 4);
  let fin = 0;
  for (;;) {
    await sender(0x0065, 4);
    recv = await readWord();
    if (((recv >> 16n) & 0xFFFFn) === 0x0075n) break;
    if (fin < 8) console.log(`[gen3] final-CRC wait reply: 0x${recv.toString(16)}`);
    if (++fin > 600) throw new Error(
      `GBA never confirmed the final CRC (last reply 0x${recv.toString(16)}).`);
  }
  await sender(0x0066, 4);
  await sender(crcC & 0xFFFF, 4);
  log("Multiboot DONE — Gen3-to-GenX should be running on the GBA.");
}
