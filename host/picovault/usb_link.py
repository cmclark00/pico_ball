"""
USB transport to the game-boy-zero-link-board running stacksmashing
gb-link-firmware.

The firmware presents a WebUSB/vendor interface (VID 0xCAFE / PID 0x4011,
interface 2, bulk OUT 0x03 / IN 0x83). The host enables the data path with a
vendor control transfer (bRequest 0x22, value 1), then exchanges one byte at a
time: write 1 byte -> the RP2040 clocks the Game Boy link -> read 1 byte back.

This matches PokemonGB_Online_Trades/usb_trading.py exactly; we keep the same
behaviour so the trade engine drives it unchanged.
"""
import sys

VID = 0xCAFE
PID = 0x4011
VENDOR_INTERFACE = 2

# Timeouts (seconds). Write waits longer; read is polled fast like the engine.
WRITE_TIMEOUT_S = 5.0
READ_TIMEOUT_S = 0.1
MAX_PACKET = 0x40


class UsbLink:
    """Opens the board and exposes the four functions the trade engine calls."""

    def __init__(self):
        try:
            import usb.core
            import usb.util
        except ImportError as exc:  # pragma: no cover - import guard
            raise RuntimeError(
                "pyusb is required. Run scripts/setup.sh, then activate "
                "host/.venv (or `pip install pyusb`)."
            ) from exc

        self._usb = usb.core
        self._util = usb.util
        self.dev = None
        self.ep_in = None
        self.ep_out = None
        self.reattach = False

    # -- lifecycle ---------------------------------------------------------
    def open(self):
        usb, util = self._usb, self._util
        dev = None
        for d in usb.find(find_all=True, idVendor=VID, idProduct=PID):
            dev = d
        if dev is None:
            raise RuntimeError(
                f"Board not found (expected USB {VID:#06x}:{PID:#06x}). "
                "Is it flashed with gb-link-firmware and plugged in? "
                "Check `lsusb` for cafe:4011."
            )

        # On Linux, detach any kernel driver bound to the interface.
        if sys.platform != "win32":
            try:
                if dev.is_kernel_driver_active(0):
                    self.reattach = True
                    dev.detach_kernel_driver(0)
            except (NotImplementedError, Exception):  # noqa: BLE001
                pass

        dev.reset()
        dev.set_configuration()
        cfg = dev.get_active_configuration()
        intf = cfg[(VENDOR_INTERFACE, 0)]

        self.ep_in = util.find_descriptor(
            intf,
            custom_match=lambda e: util.endpoint_direction(e.bEndpointAddress)
            == util.ENDPOINT_IN,
        )
        self.ep_out = util.find_descriptor(
            intf,
            custom_match=lambda e: util.endpoint_direction(e.bEndpointAddress)
            == util.ENDPOINT_OUT,
        )
        if self.ep_in is None or self.ep_out is None:
            raise RuntimeError("Could not find the vendor IN/OUT endpoints.")

        # Enable the data path (mirrors the firmware's 0x22 'connect' request).
        dev.ctrl_transfer(bmRequestType=1, bRequest=0x22, wIndex=VENDOR_INTERFACE,
                          wValue=0x01)
        self.dev = dev
        return self

    def close(self):
        if self.dev is not None:
            try:
                self._util.dispose_resources(self.dev)
                if sys.platform != "win32" and self.reattach:
                    self.dev.attach_kernel_driver(0)
            except Exception:  # noqa: BLE001
                pass
            self.dev = None

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    # -- the four functions the trade engine expects -----------------------
    def send_byte(self, byte_to_send, num_bytes):
        self.ep_out.write(
            int(byte_to_send).to_bytes(num_bytes, byteorder="big"),
            timeout=int(WRITE_TIMEOUT_S * 1000),
        )

    def receive_byte(self, num_bytes=None):
        if num_bytes is None:
            num_bytes = MAX_PACKET
        data = self.ep_in.read(num_bytes, timeout=int(READ_TIMEOUT_S * 1000))
        return int.from_bytes(data, byteorder="big")

    def send_list(self, data, chunk_size=8):
        n = len(data)
        for i in range(0, n, chunk_size):
            self.ep_out.write(bytes(data[i : i + chunk_size]),
                             timeout=int(WRITE_TIMEOUT_S * 1000))

    def receive_byte_raw(self, num_bytes=None):
        if num_bytes is None:
            num_bytes = MAX_PACKET
        return self.ep_in.read(num_bytes, timeout=int(READ_TIMEOUT_S * 1000))
