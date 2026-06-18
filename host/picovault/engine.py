"""
Glue around the vendored PokemonGB_Online_Trades engine.

We reuse its proven, sanity-checked Gen 1 trade FSM but run it *locally* (no
server, no second player) by:
  * passing a no-op connection (buffered mode keeps the whole exchange on the
    wire to the cartridge, so the network is never touched), and
  * supplying a fixed throwaway "partner" party loaded from the engine's own
    base.bin.

The engine uses paths relative to its own directory, so we chdir into it while
it runs and resolve our output paths to absolute beforehand.
"""
import os
import sys
import contextlib
from types import SimpleNamespace

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE_DIR = os.path.join(REPO_ROOT, "third_party", "PokemonGB_Online_Trades")

# Per-generation data folder under the engine's useful_data/.
GEN_FOLDER = {1: "rby", 2: "gsc", 3: "rse"}

# Gen 3 artifacts fetched by scripts/setup.sh (third_party/ is gitignored).
GEN3_DIR = os.path.join(REPO_ROOT, "third_party", "gen3")
GEN3_MULTIBOOT_GBA = os.path.join(GEN3_DIR, "pokemon_gen3_to_genx_mb.gba")


def _base_party_bin(gen):
    return os.path.join("useful_data", GEN_FOLDER[gen], "base.bin")


def _ensure_engine_importable():
    if not os.path.isdir(ENGINE_DIR):
        raise RuntimeError(
            f"Trade engine not found at {ENGINE_DIR}.\n"
            "Run ./scripts/setup.sh first (it clones the pinned engine)."
        )
    if ENGINE_DIR not in sys.path:
        sys.path.insert(0, ENGINE_DIR)


@contextlib.contextmanager
def engine_cwd():
    """Run a block with the engine directory as cwd (it uses relative paths)."""
    _ensure_engine_importable()
    prev = os.getcwd()
    os.chdir(ENGINE_DIR)
    try:
        yield
    finally:
        os.chdir(prev)


def _make_null_connection():
    """A connection whose high-level listener never blocks and never sends.

    The engine's real HighLevelListener.send_data/recv_data spin until a
    transport consumes the data; here there is no transport, so we override
    them to be immediate no-ops. In buffered mode the trade FSM never asks the
    connection for trade bytes, so this is sufficient.
    """
    from utilities.high_level_listener import HighLevelListener

    class NullHLL(HighLevelListener):
        def send_data(self, type, data):
            self.send_dict[type] = data  # remember, but don't block

        def recv_data(self, type, reset=True):
            return None  # nothing ever arrives -> engine takes the local path

    return SimpleNamespace(hll=NullHLL(), start=lambda: None, kill=lambda: None)


def _make_menu(verbose, sanity, gen):
    return SimpleNamespace(
        verbose=verbose,
        do_sanity_checks=sanity,
        japanese=False,
        buffered=True,
        gen=gen,
        multiboot=False,
        max_level=100,
    )


def build_trader(link, verbose=True, sanity=True, gen=1):
    """Construct an RBYTrading (gen 1), GSCTrading (gen 2) or RSESPTrading
    (gen 3) bound to the given UsbLink-like transport.

    Gen 3 talks to the Gen3-to-GenX program multibooted into the GBA (not to
    the game directly) over 4-byte SIO32 transfers, so the link must have been
    configured for 4-byte mode first (UsbLink.configure) and pre_sleep is on
    (the engine paces transfers so the GBA-side slave can prepare each word).
    """
    _ensure_engine_importable()
    if gen == 1:
        from utilities.rby_trading import RBYTrading as TradeClass
    elif gen == 2:
        from utilities.gsc_trading import GSCTrading as TradeClass
    elif gen == 3:
        from utilities.rse_sp_trading import RSESPTrading as TradeClass
    else:
        raise RuntimeError(f"unsupported generation: {gen}")

    connection = _make_null_connection()
    menu = _make_menu(verbose, sanity, gen)
    trader = TradeClass(
        link.send_byte,        # sending_func(byte, num_bytes)
        link.receive_byte,     # receiving_func(num_bytes)
        connection,
        menu,
        kill_function=lambda: None,
        pre_sleep=(gen == 3),
    )
    trader._pico_gen = gen
    return trader


def prime_capture_session(trader):
    """Initialize the session attributes the engine normally sets in
    pool_trade()/player_trade(). We drive enter_room()/sit_to_table()/
    trade_starting_sequence() directly, so we must set these ourselves."""
    from utilities.gsc_trading_strings import GSCTradingStrings

    trader.own_blank_trade = True
    trader.other_blank_trade = True
    trader.trade_type = GSCTradingStrings.pool_trade_str
    trader.reset_trade()
    trader.max_level = trader.menu.max_level
    trader.exit_or_new = True


def load_base_partner(trader):
    """A valid throwaway party to present to the cartridge (from base.bin)."""
    from utilities.gsc_trading_data_utils import GSCUtilsMisc

    gen = getattr(trader, "_pico_gen", 1)
    path = _base_party_bin(gen)
    base_bytes = GSCUtilsMisc.read_data(path)
    if base_bytes is None:
        raise RuntimeError(f"Could not read {path} inside the engine dir.")
    return trader.party_reader(list(base_bytes))


# --- inject (vault -> cartridge) ---------------------------------------------

# Minimum vault-record length per gen. Gens 1/2: struct + OT name + nickname.
# Gen 3: a bare .pk3 is the 100-byte party struct (names inside); the full
# engine record appends mail (36) + version info (2) + ribbon info (11) = 149.
_MIN_RECORD_LEN = {1: 0x2C + 2 * 0x0B, 2: 0x30 + 2 * 0x0B, 3: 100}
_GEN3_FULL_RECORD_LEN = 149


def build_inject_party(trader, record_bytes):
    """Build a 1-Pokémon party from a vaulted record, for Gen 1 or Gen 2.

    We build it at the object level (not by splicing raw bytes) so that
    create_trading_data writes everything at that generation's own block offsets.
    Start from base.bin's valid 1-mon party and replace the single Pokémon.
    """
    gen = getattr(trader, "_pico_gen", 1)
    if gen == 3:
        from utilities.rse_sp_trading_data_utils import RSESPTradingPokémonInfo as MonClass
    elif gen == 2:
        from utilities.gsc_trading_data_utils import GSCTradingPokémonInfo as MonClass
    else:
        from utilities.rby_trading_data_utils import RBYTradingPokémonInfo as MonClass

    rec = list(record_bytes)
    if len(rec) < _MIN_RECORD_LEN[gen]:
        raise RuntimeError(
            f"Vault record is {len(rec)} bytes; expected >= {_MIN_RECORD_LEN[gen]} "
            f"for Gen {gen}. Was it made by this tool (matching --gen)?"
        )
    if gen == 3 and len(rec) < _GEN3_FULL_RECORD_LEN:
        # A bare 100-byte .pk3: pad empty mail + version + ribbon info (the
        # capture JSON's raw_record_hex carries the lossless 149-byte form).
        rec = rec + [0] * (_GEN3_FULL_RECORD_LEN - len(rec))

    mon = MonClass.set_data(rec)                 # rebuild our mon (gen's layout)
    if gen == 3:
        # Gen 3's base.bin is a scaffold whose mon slots are intentionally
        # blank; the engine's own pool path appends the real mon object
        # (create_trading_data rebuilds the section from party.pokemon).
        from utilities.gsc_trading_data_utils import GSCUtilsMisc
        party = trader.party_reader(
            list(GSCUtilsMisc.read_data(_base_party_bin(gen))), do_full=False)
        party.pokemon.append(mon)
    else:
        party = load_base_partner(trader)        # valid 1-mon party for this gen
        party.pokemon[0] = mon
    party.party_info.total = 1
    # Gens 1/2 keep a species list beside the count; Gen 3's set_id is a no-op
    # (the species lives inside each mon struct), so this is safe for all gens.
    party.party_info.set_id(0, mon.get_species())
    return party


def local_inject_commit(trader):
    """Drive the trade-commit handshake directly with the cartridge, with no
    remote peer: we always offer our mon (party index 0) and always accept.
    Mirrors do_trade's device-facing byte sequence for that generation.

    Returns (committed: bool, given_up_index: int|None) where given_up_index is
    the cartridge's party slot it traded away (so the caller can vault it).
    """
    if getattr(trader, "_pico_gen", 1) == 3:
        return _local_inject_commit_gen3(trader)
    limit = trader.resends_limit_trade

    # 1. Read the cartridge's chosen Pokémon (0x60 + index), or a cancel.
    sent_mon = trader.wait_for_choice(trader.no_input)
    if trader.is_choice_stop(sent_mon):
        return False, None
    given_up_index = trader.convert_choice(sent_mon)

    # 2. Tell the cartridge our choice (offer party index 0 = the vaulted mon).
    our_choice = trader.first_trade_index
    nxt = trader.swap_byte(our_choice)
    nxt = trader.wait_for_no_data(nxt, our_choice, limit_resends=limit)
    if nxt == trader.no_data:
        nxt = trader.wait_for_no_input(nxt)

    # 3. Read accept/decline from the cartridge.
    accepted = trader.wait_for_accept_decline(nxt)
    if trader.is_choice_decline(accepted):
        return False, given_up_index

    # 4. Send our acceptance.
    nxt = trader.swap_byte(trader.accept_trade)
    nxt = trader.wait_for_no_data(nxt, trader.accept_trade, limit_resends=limit)
    if nxt == trader.no_data:
        nxt = trader.wait_for_no_input(nxt)

    # 5. Success handshake (commits the swap on both sides).
    success_set = trader.create_success_set([0, 0])
    success_byte = list(success_set)[0]
    trader.wait_for_success(nxt, success_set)
    nxt = trader.swap_byte(success_byte)
    nxt = trader.wait_for_no_data(nxt, success_byte, limit_resends=limit)
    if nxt == trader.no_data:
        trader.wait_for_no_input(nxt)

    return True, given_up_index


def _local_inject_commit_gen3(trader):
    """Gen 3 flavor of the local commit, mirroring RSESPTrading.do_trade's
    device-facing sequence: values are 24-bit (command byte << 16 | payload),
    there are two accept rounds and seven success rounds, and every value we
    send is repeated option_confirmation_threshold+1 times.

    NOTE: this still sends the accept/success words with empty low bits, which
    Gen3-to-GenX rejects (it re-checks the low 16 bits against the trade's
    species/PID and declines on a mismatch). The standalone firmware's
    commit_inject (firmware/path-b-standalone/gen3_trade.c) carries the correct
    payload — port that here if/when the PC-tethered Gen 3 inject is revived.
    """
    send = trader.send_data_multiple_times

    # 1. The device's chosen Pokémon ((0x80+index) << 16 | species), or stop.
    sent_mon = trader.wait_for_choice(0)
    if trader.is_choice_stop(sent_mon):
        return False, None
    given_up_index = trader.convert_choice(sent_mon)

    # 2. Offer our mon: party slot 0 (the species rides in the low bits).
    # After the starting sequence, other_pokemon is the party WE sent in, so
    # the engine's get_first_mon() builds exactly this choice value.
    send(trader.swap_trade_offer_data_pure, trader.get_first_mon())

    # 3. Two accept/decline rounds; we always accept.
    for i in range(2):
        accepted = trader.wait_for_accept_decline(0, i)
        if trader.is_choice_decline(accepted, i):
            trader.end_trade()
            return False, given_up_index
        send(trader.swap_trade_raw_data_pure, trader.accept_trade[i] << 16)

    # 4. Seven success rounds commit the swap.
    failed = False
    for i in range(7):
        success_result = trader.wait_for_success(0, i)
        failed = failed or trader.has_failed(success_result)
        send(trader.swap_trade_raw_data_pure, trader.success_trade[i] << 16)

    trader.exit_or_new = True
    trader.reset_trade()
    return (not failed), given_up_index
