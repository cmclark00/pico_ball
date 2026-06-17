// WebUSB transport to the board running Lorenzooone's reconfigurable gb-link
// firmware (third_party/gen3/gbusb_reconfigurable.uf2). This is the browser
// equivalent of host/picovault/usb_link.py and is used only for the Gen 3 path
// (Gen 1/2 use the on-device vault firmware over Web Serial instead).
//
// The firmware presents a vendor interface (VID 0xCAFE / PID 0x4011,
// interface 2, bulk OUT 0x03 / IN 0x83). We enable the data path with a control
// transfer (bRequest 0x22, value 1) and then exchange transfers of N bytes,
// where N is set by configure() (1 byte for gens 1/2, 4 for Gen 3 SIO32).
//
// IN side: WebUSB transferIn has no timeout and may split one logical SIO32
// response across packets, which byte-misaligns naive per-read code (we then
// read only a fragment of each 4-byte word). So a background pump continuously
// drains the IN endpoint into a byte FIFO, and reads consume from the FIFO with
// real timeouts and fragment reassembly — the same approach as the Web Serial
// reader in index.html. All multi-byte values are big-endian, like usb_link.py.
export const USB_VID = 0xCAFE;
export const USB_PID = 0x4011;
const VENDOR_INTERFACE = 2;
const EP_OUT = 3;            // 0x03
const EP_IN = 3;            // 0x83 -> endpoint number 3, IN direction
const MAX_PACKET = 0x40;
const READ_TIMEOUT_MS = 200;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export class UsbLink {
  constructor() {
    this.dev = null;
    this._chunks = [];        // queued IN byte chunks (Uint8Array)
    this._fifoLen = 0;        // total bytes queued
    this._closed = false;
  }

  // Prompt the user to pick the board and open it. Must be called from a user
  // gesture (click) per WebUSB rules.
  async open() {
    if (!("usb" in navigator)) {
      throw new Error("This browser has no WebUSB. Use Chrome/Edge/Comet over " +
                      "http://localhost or https.");
    }
    this.dev = await navigator.usb.requestDevice({
      filters: [{ vendorId: USB_VID, productId: USB_PID }],
    });
    await this.dev.open();
    if (this.dev.configuration === null) await this.dev.selectConfiguration(1);
    await this.dev.claimInterface(VENDOR_INTERFACE);

    // Enable the data path (the firmware's 0x22 "connect", wValue=1). The Python
    // host sends this as a *standard* request (bmRequestType=1), but WebUSB
    // forbids standard control transfers ("transfer was not allowed"). The
    // TinyUSB WebUSB firmware handles 0x22 under the CLASS type, so try class
    // first, then vendor. Best-effort: the bulk path isn't strictly gated by it.
    for (const requestType of ["class", "vendor"]) {
      try {
        const res = await this.dev.controlTransferOut({
          requestType, recipient: "interface",
          request: 0x22, value: 0x01, index: VENDOR_INTERFACE,
        });
        if (res.status === "ok") { this._connectType = requestType; break; }
      } catch (e) { /* try the next type */ }
    }

    this._closed = false;
    this._startPump();
    return this;
  }

  async close() {
    this._closed = true;
    if (!this.dev) return;
    try { await this.dev.releaseInterface(VENDOR_INTERFACE); } catch (e) {}
    try { await this.dev.close(); } catch (e) {}
    this.dev = null;
  }

  // -- background IN pump + FIFO ---------------------------------------------

  _startPump() {
    (async () => {
      while (!this._closed && this.dev) {
        let res;
        try {
          res = await this.dev.transferIn(EP_IN, MAX_PACKET);
        } catch (e) {
          if (this._closed) break;
          await sleep(5);
          continue;
        }
        if (res && res.status === "ok" && res.data && res.data.byteLength) {
          this._chunks.push(new Uint8Array(
            res.data.buffer, res.data.byteOffset, res.data.byteLength));
          this._fifoLen += res.data.byteLength;
        } else if (res && res.status === "stall") {
          try { await this.dev.clearHalt("in", EP_IN); } catch (e) {}
        }
      }
    })();
  }

  clearFifo() { this._chunks = []; this._fifoLen = 0; }

  // Wait until the FIFO has data or `timeoutMs` elapses; returns true if data.
  async _waitData(timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    while (this._fifoLen === 0 && Date.now() < deadline) await sleep(2);
    return this._fifoLen > 0;
  }

  // Remove and return up to `n` bytes from the FIFO (front).
  _take(n) {
    const take = Math.min(n, this._fifoLen);
    const out = new Uint8Array(take);
    let got = 0;
    while (got < take) {
      const c = this._chunks[0];
      const need = take - got;
      if (c.length <= need) { out.set(c, got); got += c.length; this._chunks.shift(); }
      else { out.set(c.subarray(0, need), got); this._chunks[0] = c.subarray(need); got += need; }
    }
    this._fifoLen -= take;
    return out;
  }

  // Read one logical response: wait up to `timeoutMs` for the first byte, then
  // keep absorbing bytes while they keep arriving within `gapMs` (reassembling a
  // word that WebUSB split across packets). Returns a big-endian BigInt, or 0n on
  // timeout. Mirrors multiboot.py read_all but is fragment-safe.
  async readAllValue(timeoutMs = READ_TIMEOUT_MS, gapMs = 15) {
    if (!(await this._waitData(timeoutMs))) return 0n;
    let out = 0n;
    for (;;) {
      const chunk = this._take(this._fifoLen);
      for (const b of chunk) out = (out << 8n) | BigInt(b);
      if (!(await this._waitData(gapMs))) break;
    }
    return out;
  }

  // Read exactly `numBytes` (one SIO32 transfer width) and return as a
  // big-endian integer (Number). Waits up to `timeoutMs` and reassembles
  // fragmented packets, so it never returns a partial word mid-protocol. This is
  // the IN half of swap_byte; mirrors usb_link.py receive_byte.
  async receiveByte(numBytes, timeoutMs = READ_TIMEOUT_MS) {
    const deadline = Date.now() + timeoutMs;
    while (this._fifoLen < numBytes && Date.now() < deadline) await sleep(1);
    const bytes = this._take(Math.min(numBytes, this._fifoLen));
    let v = 0;
    for (const b of bytes) v = v * 256 + b;
    return v;
  }

  // Discard anything buffered (and any stragglers within 50 ms).
  async drain() {
    for (;;) {
      this.clearFifo();
      if (!(await this._waitData(50))) return;
    }
  }

  // -- the OUT functions multiboot / the engine expect -----------------------

  // Send one value as `numBytes` big-endian bytes.
  async sendByte(value, numBytes) {
    const buf = new Uint8Array(numBytes);
    let v = value >>> 0;
    for (let i = numBytes - 1; i >= 0; i--) { buf[i] = v & 0xFF; v = Math.floor(v / 256); }
    await this._out(buf);
  }

  // Send a byte list in chunks (default 8, like usb_link.send_list). Verifies
  // each transfer fully completed — a short/stalled bulk OUT would drop bytes.
  async sendList(data, chunkSize = 8) {
    const arr = Uint8Array.from(data);
    for (let i = 0; i < arr.length; i += chunkSize) {
      await this._out(arr.subarray(i, i + chunkSize));
    }
  }

  async _out(chunk) {
    const res = await this.dev.transferOut(EP_OUT, chunk);
    if (res.status !== "ok" || res.bytesWritten !== chunk.length) {
      throw new Error(`Bulk OUT incomplete (status ${res.status}, ` +
        `wrote ${res.bytesWritten}/${chunk.length})`);
    }
  }

  // Send the reconfigurable-firmware config packet (transfer width + pacing).
  // Returns true when the firmware acknowledges (ack low byte == 1). Mirrors
  // UsbLink.configure() in host/picovault/usb_link.py.
  async configure(bytesPerTransfer, usBetween = 1000) {
    const cfg = [];
    for (let i = 0; i < 8; i++) cfg.push(0xCA, 0xFE);
    for (let i = 0; i < 4; i++) cfg.push(0xDE, 0xAD, 0xBE, 0xEF);
    cfg.push(usBetween & 0xFF, (usBetween >> 8) & 0xFF,
             (usBetween >> 16) & 0xFF, bytesPerTransfer & 0xFF);
    await this.drain();
    await this.sendList(cfg, cfg.length);
    const ack = await this.readAllValue(800, 15);
    console.log("configure() ack value: 0x" + ack.toString(16));
    return (ack & 0xFFn) === 1n;
  }
}
