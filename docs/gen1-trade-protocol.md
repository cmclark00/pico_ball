# Gen 1 (RBY) link‑trade protocol — byte level

This is the reference for Path B (where we re‑implement the FSM on the RP2040) and
for debugging Path A. Constants below are confirmed against
`PokemonGB_Online_Trades/utilities/rby_trading.py`.

## Electrical / timing

- 8‑bit, MSB‑first, synchronous serial. One side supplies the clock (~8 kHz on
  real hardware → ~1 KB/s).
- **Path A:** our RP2040 firmware is the **master** (it clocks); the host paces.
- **Path B:** our RP2040 will be the **slave** (the Game Boy clocks); we must have
  the next byte loaded before each clock and respond within ~120 µs.
- Every transfer is a *swap*: you put a byte in the serial register, a clock
  happens, you read the byte the other side put in. There is no "receive without
  send."

## Control bytes

| Byte | Meaning |
|---|---|
| `0x00` | blank / no data |
| `0x01` | master magic (in the connect handshake) |
| `0x02` | slave magic |
| `0x60`–`0x6F` | in‑room filler / selection (`0x60 + party_index`); `0x6F` = stop/cancel |
| `0xD0` | choose **Colosseum** (battle) room |
| `0xD4` | choose **Trade Center** (trade) room |
| `0xFD` | section preamble / "next section" sync byte |
| `0xFE` | placeholder that must be patched out of payload (hence patch lists) |

(From `rby_trading.py`: `next_section=0xFD`, `no_input=0xFE`,
`stop_trade=0x6F`, `first_trade_index=0x60`, `decline_trade=0x61`,
`accept_trade=0x62`.)

## Phases

1. **Enter room.** The game and partner exchange `0x01/0x60/0xD0/0xD4`. The engine
   models the acceptable response sets as:
   - `enter_room_states = [[0x01,0x60,0xD0,0xD4], [ {0x60..0x6F}, {0xD0..0xD4}, {0xD0..0xD4}, {0x60..0x6F} ]]`
   We steer toward `0xD4` (Trade Center).

2. **Start trading.** `start_trading_states = [[0x60,0x60], [{0x60..0x6F}, {0xFD}]]`
   — filler `0x60`s until the `0xFD` preamble of the first data section appears.

3. **Three data sections** (`special_sections_len = [0xA, 0x1A2, 0xC5]`, each
   preceded by `special_sections_preamble_len = [7, 6, 3]` sync bytes):
   - **Section 0 — random seed** (`0xA` = 10 bytes): RNG sync for the trade
     animation.
   - **Section 1 — trade block / party** (`0x1A2` = 418 bytes incl. framing; the
     party payload is ~415): party count, species list, then each Pokémon's
     **44‑byte (`0x2C`)** structure, OT names, nicknames.
   - **Section 2 — patch list** (`0xC5`): positions where `0xFE` bytes in the party
     section were substituted, so the raw `0xFE` control byte never appears in
     payload. Applied via `apply_patches()` to reconstruct the true bytes.

4. **Selection & confirm.** Player picks a mon → `0x60 + index`. The partner sends
   its choice; both confirm (`0x62`) or decline (`0x61`); `0x6F` cancels. Only here
   does the swap commit.

## The non‑destructive copy

The full party (section 1) arrives in **phase 3**, before any commit in phase 4.
`extract.py`:
- runs phases 1–3 (`enter_room` + `trade_starting_sequence`),
- parses section 1 into 6 Pokémon and saves them,
- then refuses/cancels in phase 4 (sends `stop_trade`/disconnects),

so the cartridge's data is never modified.

## Gen 1 Pokémon structure (44 bytes, `0x2C`)

Offsets used by the engine (`RBYTradingPokémonInfo`): species `0x00`, current HP
`0x01`, level (box) `0x03`, status `0x04`, types `0x05`, item/held `0x07`, moves
`0x08`–`0x0B`, OT ID `0x0C`, EXP `0x0E`, stat experience `0x11`, IVs `0x1B`, PP
`0x1D`, level `0x21`, max HP/stats `0x22`+. Nickname and OT name are stored
separately in the trade block (11 bytes each, GB text encoding). Full table:
Bulbapedia "Pokémon data structure (Generation I)".
