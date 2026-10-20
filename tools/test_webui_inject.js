// Exercise the actual page's injection routing with a simulated vault board.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    addEventListener() {}, style: {}, textContent: "", disabled: false
  });
  return elements.get(id);
}
const context = vm.createContext({
  console, TextEncoder, TextDecoder, Uint8Array, setTimeout, clearTimeout,
  window: {addEventListener() {}}, navigator: {usb: {addEventListener() {}}},
  document: {getElementById: element}
});
const html = fs.readFileSync(path.join(__dirname, "../webui/index.html"), "utf8");
vm.runInContext(html.match(/<script type="module">([\s\S]*?)<\/script>/)[1], context);
const run = code => vm.runInContext(code, context);
run(String.raw`
  let writes = [], boardGen = "1", acknowledgement = null;
  let injectResult = "INJECT_RESULT ok";
  usbDev = {};
  sendCommand = async (payload, done, timeout, onLine) => {
    writes.push(payload);
    let line;
    if (/^[12]$/.test(payload)) {
      boardGen = payload;
      line = acknowledgement || "GEN " + boardGen;
    } else if (/^i[0-9]+\n$/.test(payload)) {
      line = injectResult;
    } else throw new Error("Unexpected command: " + payload);
    if (onLine) onLine(line);
    if (!done(line)) throw new Error("Vault command timed out");
  };
  dump = async () => { writes.push("d"); setStatus("Connected"); };
`);
(async () => {
  for (const targetGen of ["1", "2"]) {
    for (const sourceGen of [1, 2]) {
      element("gen").value = targetGen;
      run('writes = []; boardGen = "1";');
      await run(`injectRecord(4, ${sourceGen})`);
      assert.equal(run('writes.join("|")'), targetGen + "|i4\n|d");
      assert.equal(run("boardGen"), targetGen);
      assert.equal(element("status").textContent, "Injected!");
    }
  }
  run('writes = []; injectResult = "INJECT_RESULT fail -1";');
  await run("injectRecord(4, 2)");
  assert.match(element("status").textContent, /INJECT_RESULT fail -1/);
  run('writes = []; acknowledgement = "GEN 1";');
  await run("injectRecord(4, 2)");
  assert.equal(run('writes.join("|")'), "2");
  assert.match(element("status").textContent, /did not confirm/);
  element("gen").value = "3";
  run("writes = [];");
  await run("injectRecord(4, 2)");
  assert.equal(run("writes.length"), 0);
  assert.match(element("status").textContent, /destination cartridge/);
  console.log("WebUI injection passed: destination protocol, cross-gen routing, acknowledgement, retained results");
})().catch(error => { console.error(error); process.exitCode = 1; });
