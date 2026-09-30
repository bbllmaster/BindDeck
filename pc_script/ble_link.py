"""BLE transport for BindDeck - CFG:/CMD: over a secondary GATT service.

The firmware exposes a vendor service next to HID on the same BLE connection:

    service  b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f
    RX       b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f   write    (PC -> device)
    TX       b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f   notify   (device -> PC)

This module owns an asyncio loop on a daemon thread, keeps the connection
alive (auto-reconnect + backoff), writes command lines to RX and hands every
TX notification to a callback. The lines are identical to what the USB serial
and WiFi-UDP paths produce, so the app's parser is shared.

The characteristics are encrypted-only, so an unbonded client gets a security
error; we pair on demand and retry once.
"""

import asyncio
import threading

BLEAK_IMPORT_ERROR = None
try:
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakGATTProtocolError

    BLEAK_AVAILABLE = True
except Exception as _e:  # bleak not installed - app still runs on WiFi/serial
    BLEAK_AVAILABLE = False
    BLEAK_IMPORT_ERROR = f"{type(_e).__name__}: {_e}"

SVC = "b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f"
RX = "b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f"
TX = "b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f"

DEVICE_NAME = "BindDeck"
SCAN_TIMEOUT = 10.0
RECONNECT_DELAY = 3.0


def _is_security_error(err) -> bool:
    e = str(err).lower()
    return "nsum" in e or "uthentic" in e or "ncrypt" in e


class BleLink:
    def __init__(self, on_line=None):
        self._on_line = on_line
        self._loop = None
        self._queue = None
        self._connected = False
        self._address = None
        self._lock = threading.Lock()

    # ---- thread-safe public API -------------------------------------------
    @property
    def available(self) -> bool:
        return BLEAK_AVAILABLE

    @property
    def connected(self) -> bool:
        with self._lock:
            return self._connected

    @property
    def address(self):
        with self._lock:
            return self._address

    def start(self) -> bool:
        if not BLEAK_AVAILABLE:
            return False
        threading.Thread(target=self._thread_main, daemon=True, name="ble-link").start()
        return True

    def send(self, cmd: str) -> bool:
        """Queue one CFG:/CMD: line. Returns False when not connected."""
        with self._lock:
            if not self._connected or self._loop is None or self._queue is None:
                return False
            loop, queue = self._loop, self._queue
        try:
            loop.call_soon_threadsafe(queue.put_nowait, cmd)
            return True
        except Exception:
            return False

    # ---- internals --------------------------------------------------------
    def _thread_main(self):
        try:
            asyncio.run(self._runner())
        except Exception as e:  # noqa: BLE001
            print("[ble] fatal:", type(e).__name__, e)

    async def _runner(self):
        self._loop = asyncio.get_running_loop()
        self._queue = asyncio.Queue()
        while True:
            try:
                await self._session()
            except Exception as e:  # noqa: BLE001
                print("[ble] session error:", type(e).__name__, e)
            with self._lock:
                self._connected = False
            await asyncio.sleep(RECONNECT_DELAY)

    async def _find(self):
        # Cached address first (fast); the device advertises even while
        # connected, so a scan is a reliable fallback.
        with self._lock:
            addr = self._address
        if addr:
            try:
                client = BleakClient(addr, timeout=20.0)
                await client.connect()
                if client.is_connected:
                    return client
                await client.disconnect()
            except Exception as e:  # noqa: BLE001
                print("[ble] cached address failed:", e)
        dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT)
        if dev is None:
            return None
        with self._lock:
            self._address = dev.address
        client = BleakClient(dev, timeout=20.0)
        await client.connect()
        return client if client.is_connected else None

    async def _subscribe(self, client):
        def _tx(_c, data: bytearray):
            for one in data.decode("utf-8", errors="ignore").splitlines():
                one = one.strip()
                if one and self._on_line:
                    try:
                        self._on_line(one)
                    except Exception as e:  # noqa: BLE001
                        print("[ble] line handler error:", e)

        try:
            await client.start_notify(TX, _tx)
        except BleakGATTProtocolError as e:
            if not _is_security_error(e):
                raise
            print("[ble] notify needs security - pairing ...")
            await client.pair()
            await client.start_notify(TX, _tx)
        return _tx

    async def _write(self, client, cmd: str):
        try:
            await client.write_gatt_char(RX, cmd.encode(), response=True)
        except BleakGATTProtocolError as e:
            if not _is_security_error(e):
                raise
            print("[ble] write needs security - pairing ...")
            await client.pair()
            await client.write_gatt_char(RX, cmd.encode(), response=True)

    async def _session(self):
        client = await self._find()
        if client is None:
            print("[ble] device not found (is it powered / in range?)")
            return
        print(f"[ble] connected {client.address} mtu={client.mtu_size}")
        try:
            await self._subscribe(client)
            with self._lock:
                self._connected = True
            while client.is_connected:
                try:
                    cmd = await asyncio.wait_for(self._queue.get(), timeout=1.0)
                except asyncio.TimeoutError:
                    continue
                try:
                    await self._write(client, cmd)
                except Exception as e:  # noqa: BLE001
                    print("[ble] write error:", e)
        finally:
            with self._lock:
                self._connected = False
            try:
                await client.disconnect()
            except Exception:
                pass
            print("[ble] disconnected")
