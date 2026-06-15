#!/usr/bin/env python3
"""Generate webui/gen3_partner.js: the baked Gen 3 throwaway partner party as
the 896-byte trading section (from the engine's rse/base.bin). The WebUI presents
this to the GBA during a Gen 3 capture, then cancels (nothing is committed)."""
import base64, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "host"))
from picovault import engine


class DummyLink:
    def send_byte(self, *a): pass
    def receive_byte(self, *a): return 0


def main():
    with engine.engine_cwd():
        trader = engine.build_trader(DummyLink(), verbose=False, sanity=True, gen=3)
        engine.prime_capture_session(trader)
        partner = engine.load_base_partner(trader)
        section = bytes(partner.create_trading_data(trader.special_sections_len)[0])
    assert len(section) == 0x380, len(section)
    b64 = base64.b64encode(section).decode()
    out = os.path.join(REPO, "webui", "gen3_partner.js")
    with open(out, "w") as f:
        f.write(
            "// Baked Gen 3 throwaway partner party as the 896-byte (0x380) trading\n"
            "// section, from the engine's useful_data/rse/base.bin via\n"
            "// create_trading_data. Presented to the GBA during a Gen 3 capture, then\n"
            "// cancelled (nothing committed). Regenerate: python tools/gen_gen3_partner.py\n"
            f'const B64 = "{b64}";\n'
            "const bin = atob(B64);\n"
            "export const GEN3_PARTNER_SECTION = new Uint8Array(bin.length);\n"
            "for (let i = 0; i < bin.length; i++) GEN3_PARTNER_SECTION[i] = bin.charCodeAt(i);\n"
        )
    print("wrote", out, "(", len(section), "bytes )")


if __name__ == "__main__":
    main()
