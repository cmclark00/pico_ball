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
GEN_FOLDER = {1: "rby", 2: "gsc"}


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
    """Construct an RBYTrading (gen 1) or GSCTrading (gen 2) bound to the
    given UsbLink-like transport. The capture flow is identical for both."""
    _ensure_engine_importable()
    if gen == 1:
        from utilities.rby_trading import RBYTrading as TradeClass
    elif gen == 2:
        from utilities.gsc_trading import GSCTrading as TradeClass
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
        pre_sleep=False,
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

# Minimum vault-record length per gen: struct + OT name + nickname.
_MIN_RECORD_LEN = {1: 0x2C + 2 * 0x0B, 2: 0x30 + 2 * 0x0B}  # 66 (RBY), 70 (GSC)


def build_inject_party(trader, record_bytes):
    """Build a 1-Pokémon party from a vaulted record, for Gen 1 or Gen 2.

    We build it at the object level (not by splicing raw bytes) so that
    create_trading_data writes everything at that generation's own block offsets.
    Start from base.bin's valid 1-mon party and replace the single Pokémon.
    """
    gen = getattr(trader, "_pico_gen", 1)
    if gen == 2:
        from utilities.gsc_trading_data_utils import GSCTradingPokémonInfo as MonClass
    else:
        from utilities.rby_trading_data_utils import RBYTradingPokémonInfo as MonClass

    rec = list(record_bytes)
    if len(rec) < _MIN_RECORD_LEN[gen]:
        raise RuntimeError(
            f"Vault record is {len(rec)} bytes; expected >= {_MIN_RECORD_LEN[gen]} "
            f"for Gen {gen}. Was it made by this tool (matching --gen)?"
        )

    party = load_base_partner(trader)            # valid 1-mon party for this gen
    mon = MonClass.set_data(rec)                 # rebuild our mon (gen's layout)
    party.pokemon[0] = mon
    party.party_info.total = 1
    party.party_info.actual_mons[0] = mon.get_species()
    return party


def local_inject_commit(trader):
    """Drive the Gen 1 trade-commit handshake directly with the cartridge,
    with no remote peer: we always offer our mon (party index 0) and always
    accept. Mirrors GSCTrading.do_trade's device-facing byte sequence.

    Returns (committed: bool, given_up_index: int|None) where given_up_index is
    the cartridge's party slot it traded away (so the caller can vault it).
    """
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
