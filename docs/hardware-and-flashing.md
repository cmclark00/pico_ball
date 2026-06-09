# Hardware setup, cabling, and doing your first trade

## What you need

- The assembled `game-boy-zero-link-board` (RP2040‑Zero + level shifter + edge
  connectors). ✅ you have this.
- A **GBA** (or GBA SP) and your **Pokémon Blue** cartridge.
- A **Game Boy / GBA link cable** that mates with the board's **GBC/GBA edge
  connector** (J1). The board accepts a link cable plugged into the edge connector;
  the other end of that cable goes into the GBA's link port.
- A USB cable from the board to your laptop.

## Physical connection

```
[Pokémon Blue in GBA] --link cable--> [board J1 GBC/GBA connector]
                                            |
                                        RP2040-Zero
                                            |
                                          USB  --> [your laptop]
```

The GBA runs Pokémon Blue in Game Boy compatibility mode; electrically the link is
the classic GB serial link, which is what the board and firmware speak.

## Flash the firmware

See [../firmware/README.md](../firmware/README.md). End result: `lsusb` shows
`cafe:4011`.

## Linux USB permissions

`scripts/setup.sh` prints a udev rule. Install it so you don't need `sudo`:

```bash
sudo tee /etc/udev/rules.d/99-gb-link.rules >/dev/null <<'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4011", MODE="0666"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

(Re‑plug the board after installing the rule.)

## Step 0 — prove the link works (recommended before the bespoke extractor)

The lowest‑risk proof that your soldering + firmware + cartridge all work is to do
one real trade with the upstream engine:

```bash
source host/.venv/bin/activate
cd third_party/PokemonGB_Online_Trades
python usb_trading.py
# In the menu choose: Gen 1 -> Pool Trade (trades against the public server)
```

In the game: **Pokémon Center → upstairs → talk to the Link receptionist → Trade
Center → walk to the table and interact.** If a Pokémon swaps, everything below
the software is good. (Pool trade *will* swap a Pokémon — use a spare/expendable
one, or just use it as a connectivity test and decline.)

## Step 1 — copy your party into the vault (non‑destructive)

```bash
source host/.venv/bin/activate
python host/extract.py
```

What happens:
1. Sit at the Trade Center table in‑game and select your first Pokémon when the
   tool tells you it's connected.
2. The tool reads the trade block (your whole party), writes each Pokémon to
   `vault/`, then **cancels** — nothing leaves your cartridge.

Outputs in `vault/`:
- `partyNN_<species>.pk1` — raw 44‑byte Gen 1 structure (re‑importable later).
- `partyNN_<species>.json` — decoded: species, nickname, level, moves+PP, OT
  name/ID, IVs, stat experience, current HP/status.
- `party.json` — the whole party in one file.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Device not found` | firmware not flashed / wrong VID:PID / permissions | re‑flash; check `lsusb` for `cafe:4011`; install udev rule |
| Opens but trade never starts | not actually at the Trade Center table, or cable in wrong port | use the Cable Club **Trade Center**, reseat the link cable in J1 and the GBA |
| Starts then stalls / garbage bytes | link wiring / level‑shift / pacing | confirm with Step 0 pool trade; try the reconfigurable firmware; capture with a logic analyzer on GPIO 0/1/2 |
| Works on DMG/GBC but not GBA | GBA link‑mode quirk or cable | verify the cable is fully seated; some third‑party GBA cables are flaky |
| Sanity‑check rejects data | corrupted/odd party member | run with `--no-sanity` to capture raw bytes for inspection |
