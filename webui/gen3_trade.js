// Gen 3 trade-partner capture, ported from the capture path of
// third_party/PokemonGB_Online_Trades/utilities/rse_sp_trading.py (RSESPTrading).
// We act as the trade peer to the Gen3-to-GenX program running on the GBA: present
// a baked throwaway party and read back the cartridge's 896-byte party section,
// then slice it into 100-byte .pk3 records (decoded by decodeMonRecord elsewhere).
//
// Only the buffered single-section exchange (read_section) + its helpers and the
// section checksum are ported — enough to CAPTURE. We bake the partner section
// (gen3_partner.js) rather than port create_trading_data, and skip the engine's
// optional species sanity checks (the checksum gates completion).
import { GEN3_PARTNER_SECTION } from "./gen3_partner.js";

// --- RSESPTrading constants --------------------------------------------------
const SECTION_LEN = 0x380;                 // special_sections_len[0] = 896
const ASKING_DATA_NYBBLE = 0xC;
const DONE_FLAG = 0x20;                     // done_control_flag
const NOT_DONE_FLAG = 0x40;                // not_done_control_flag
const SENDING_DATA_FLAG = 0x10;            // sending_data_control_flag
const IN_PARTY_FLAG = 0x80;                // in_party_trading_flag
const SINCE_LAST_USEFUL_LIMIT = 10;
const BASE_SEND_DATA_START = 1;
const BASE_DATA_CHUNK_SIZE = 0xFE;
const TRADE_CANCEL = 0x8F;
const STOP_TRADE = TRADE_CANCEL << 16;
const OPTION_CONFIRM_THRESHOLD = 10;

// RSESPTradingData section layout (0x380 section)
const TRADING_MAIL_POS = 8;
const TRADING_MAIL_LENGTH = 0x24;
const TRADING_PARTY_MAX = 6;
const TRADING_PARTY_INFO_POS = 0xE4;
const TRADING_POKEMON_POS = 0xE8;
const TRADING_POKEMON_LENGTH = 0x64;       // 100 -> exactly a .pk3 party struct

const u32 = (b, p) => (b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | (b[p + 3] << 24)) >>> 0;

// One SIO32 word exchange (gen3 pre_sleep=True: firmware paces, no host sleep).
async function swapByte(link, sendData) {
  await link.sendByte(sendData >>> 0, 4);
  return await link.receiveByte(4);
}

function getBytesFromPos(index) {
  let basePos = index & 0xFFF;
  let byteBase = BASE_SEND_DATA_START;
  while (basePos >= BASE_DATA_CHUNK_SIZE) { basePos -= BASE_DATA_CHUNK_SIZE; byteBase++; }
  return (byteBase << 8) | basePos;
}

function getPosFromBytes(value) {
  let finalPos = value & 0xFF;
  if (finalPos >= BASE_DATA_CHUNK_SIZE) finalPos = 0;
  return finalPos + BASE_DATA_CHUNK_SIZE * (((value >> 8) & 0xF) - BASE_SEND_DATA_START);
}

// Returns {next, position, isValid, isAsking, isComplete, isDone, otherPos, otherEnd}
function interpretSetup(data) {
  let next = data & 0xFFFF;
  let position = (data >>> 16) & 0xFF;
  let controlByte = (data >>> 24) & 0xFF;
  let otherPos = data & 0xFFF;
  let otherEnd = (data >>> 12) & 0xFFF;
  let isValid = false, isAsking = false, isComplete = false, isDone = false;

  if ((controlByte & 0xF) >= ASKING_DATA_NYBBLE) {
    controlByte &= ~SENDING_DATA_FLAG;
    if ((controlByte & NOT_DONE_FLAG) !== 0) {
      isAsking = true;
      if (otherEnd > (SECTION_LEN >> 1)) otherEnd = SECTION_LEN >> 1;
      if (otherPos >= otherEnd) otherPos = otherEnd;
    } else if ((controlByte & DONE_FLAG) !== 0) {
      otherPos = otherEnd;
    }
  }
  if ((controlByte & SENDING_DATA_FLAG) !== 0) {
    let recvPos = getPosFromBytes(data >>> 16);
    position = recvPos;
    isValid = true;
    if (recvPos >= (SECTION_LEN >> 1)) {
      recvPos = 0;
      controlByte &= ~SENDING_DATA_FLAG;
      isValid = false;
    }
  }
  if ((controlByte & DONE_FLAG) !== 0) {
    isDone = true;
    if (controlByte & IN_PARTY_FLAG) isComplete = true;
  }
  return { next, position, isValid, isAsking, isComplete, isDone, otherPos, otherEnd };
}

async function swapTradeSetupData(link, next, index, isComplete) {
  let data = next;
  data |= (isComplete ? DONE_FLAG : NOT_DONE_FLAG) << 24;
  data |= SENDING_DATA_FLAG << 24;
  data |= getBytesFromPos(index) << 16;
  data |= next & 0xFFFF;
  return interpretSetup(await swapByte(link, data >>> 0));
}

async function askTradeSetupData(link, start, end) {
  let data = 0;
  data |= (NOT_DONE_FLAG | ASKING_DATA_NYBBLE) << 24;
  data |= start & 0xFFF;
  data |= (end & 0xFFF) << 12;
  return interpretSetup(await swapByte(link, data >>> 0));
}

function findUncompletedRange(completed) {
  let i = 0, maxSize = 0, maxStart = 0, maxEnd = 0;
  while (i < completed.length) {
    let k = i;
    for (let l = i; l < completed.length; l++) { if (completed[l]) break; k++; }
    if (k - i > maxSize) { maxSize = k - i; maxStart = i; maxEnd = k; }
    if (k !== i) i = k; else i++;
  }
  return [maxStart, maxEnd];
}

// Section checksum (RSESPTradingData.are_checksum_valid) — gates completion.
function checksumsValid(buf) {
  let checksum = 0;
  for (let i = 0; i < TRADING_PARTY_MAX; i++)
    for (let j = 0; j < TRADING_MAIL_LENGTH / 4; j++)
      checksum = (checksum + u32(buf, i * TRADING_MAIL_LENGTH + j * 4 + TRADING_MAIL_POS)) >>> 0;
  if (u32(buf, TRADING_MAIL_POS + TRADING_PARTY_MAX * TRADING_MAIL_LENGTH) !== checksum) return false;

  checksum = u32(buf, TRADING_PARTY_INFO_POS);
  for (let i = 0; i < TRADING_PARTY_MAX; i++)
    for (let j = 0; j < TRADING_POKEMON_LENGTH / 4; j++)
      checksum = (checksum + u32(buf, i * TRADING_POKEMON_LENGTH + j * 4 + TRADING_POKEMON_POS)) >>> 0;
  if (u32(buf, TRADING_POKEMON_POS + TRADING_PARTY_MAX * TRADING_POKEMON_LENGTH) !== checksum) return false;

  checksum = 0;
  for (let i = 0; i < (SECTION_LEN - 4) / 4; i++) checksum = (checksum + u32(buf, i * 4)) >>> 0;
  if (u32(buf, SECTION_LEN - 4) !== checksum) return false;
  return true;
}

// Port of RSESPTrading.read_section for the capture direction: send our section,
// fill `buf` with the cartridge's section, return buf once checksum-complete.
async function readSection(link, sendData, log = () => {}) {
  const length = SECTION_LEN;
  const half = length >> 1;
  const completed = new Array(half).fill(false);
  const buf = new Uint8Array(length);
  let numUncompleted = half;
  let otherPos = 0, otherEnd = 0;
  let next = 0;
  let sinceLastUseful = SINCE_LAST_USEFUL_LIMIT;
  let transferSuccessful = false;
  let hasAllData = false;
  let guard = 0;
  const GUARD_MAX = 4_000_000;       // safety: ~minutes of polling, never infinite

  while (!transferSuccessful) {
    if (++guard > GUARD_MAX)
      throw new Error("Gen 3 capture timed out exchanging the party section. " +
        "Is the GBA on the Gen3-to-GenX trade screen?");

    let r;
    if (sinceLastUseful >= SINCE_LAST_USEFUL_LIMIT && !hasAllData) {
      const [start, end] = findUncompletedRange(completed);
      r = await askTradeSetupData(link, start, end);
      sinceLastUseful = 0;
    } else {
      if (sendData != null) {
        next = otherPos < otherEnd
          ? (sendData[otherPos * 2] | (sendData[otherPos * 2 + 1] << 8)) : 0;
      } else { next = 0; otherPos = 0; }
      r = await swapTradeSetupData(link, next, otherPos, hasAllData);
      if (otherPos < otherEnd) otherPos += 1;
      if (otherPos >= half) otherPos = 0;
    }
    sinceLastUseful += 1;

    if (r.isAsking) {
      otherPos = r.otherPos;
      otherEnd = r.otherEnd;
    } else if (!(r.isDone && r.isComplete)) {
      if (!hasAllData && r.isValid) {
        buf[r.position * 2] = r.next & 0xFF;
        buf[r.position * 2 + 1] = (r.next >> 8) & 0xFF;
        if (!completed[r.position]) {
          sinceLastUseful = 0;
          completed[r.position] = true;
          numUncompleted -= 1;
          if (numUncompleted === 0) {
            if (checksumsValid(buf)) {
              hasAllData = true;
              if (sendData == null) transferSuccessful = true;
            } else {
              completed.fill(false);
              sinceLastUseful = SINCE_LAST_USEFUL_LIMIT;
              numUncompleted = half;
            }
          }
        }
      }
    } else {
      if (hasAllData) transferSuccessful = true;
    }
    if ((guard & 0x3FF) === 0)
      log(`Capturing party… ${length - numUncompleted * 2}/${length} bytes`);
  }
  return buf;
}

// Best-effort: back out of the trade menu so the GBA returns cleanly (port of
// RSESPTrading.end_trade, with an iteration cap).
async function endTrade(link) {
  const offerCancel = async () => {
    const data = ((DONE_FLAG | IN_PARTY_FLAG) << 24 | (TRADE_CANCEL << 16)) >>> 0;
    const recv = await swapByte(link, data);
    // interpret_in_data_trade_gen3: valid only if control byte matches.
    return ((recv >>> 24) & 0xFF) === (IN_PARTY_FLAG | DONE_FLAG) ? (recv & 0xFFFFFF) : null;
  };
  let next = null, tries = 0;
  while ((next === null || (next & 0xFF0000) !== STOP_TRADE) && tries++ < 2000) {
    next = await offerCancel();
  }
  for (let i = 0; i <= OPTION_CONFIRM_THRESHOLD; i++) {
    const data = ((DONE_FLAG | IN_PARTY_FLAG) << 24 | STOP_TRADE) >>> 0;
    await swapByte(link, data);
  }
}

// Capture the cartridge's party. Returns an array of 100-byte .pk3 records.
export async function captureGen3Party(link, log = () => {}) {
  await link.drain();                       // clear any multiboot leftovers
  const buf = await readSection(link, GEN3_PARTNER_SECTION, log);
  const count = Math.min(u32(buf, TRADING_PARTY_INFO_POS), TRADING_PARTY_MAX);
  const mons = [];
  for (let i = 0; i < count; i++) {
    const off = TRADING_POKEMON_POS + i * TRADING_POKEMON_LENGTH;
    mons.push(buf.slice(off, off + TRADING_POKEMON_LENGTH));
  }
  log(`Captured ${mons.length} Pokémon. Backing out of the trade…`);
  try { await endTrade(link); } catch (e) { /* best-effort */ }
  return mons;
}
