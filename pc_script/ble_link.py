"""BLE transport for BindDeck - CFG:/CMD: over a secondary GATT service.

The firmware exposes a vendor service next to HID on the same BLE connection:

    service  b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f
    RX       b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f   write    (PC -> device)
    TX       b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f   notify   (device -> PC)

Multiple BindDecks may be connected at once. Each device broadcasts a *unique*
BLE name ``BindDeck-XXXX`` (XXXX = last two MAC bytes); the side-channel
``BTN:<devId>:<btn>`` message carries that suffix so the PC can tell them
apart. This module therefore manages one connection per device through a
``BleLinkManager``:

    manager = BleLinkManager(on_line=handle_device_line)
    manager.start()                 # begins discovery + connects every deck
    manager.send(dev_id, "CFG:..")  # route a command to one device
    manager.send_all("CMD:..")      # broadcast to every connected deck

The per-device ``BleLink`` keeps its own asyncio loop on a daemon thread,
stays connected (auto-reconnect + backoff), writes command lines to its RX and
hands every TX notification to the manager's callback *tagged with the device
id* (``on_line(dev_id, line)``) so the app never loses the source. The lines
are identical to what the USB serial and WiFi-UDP paths produce, so the app's
parser is shared.

The characteristics are encrypted-only, so an unbonded client gets a security
error; we pair on demand and retry once.
"""

import asyncio
import threading
import traceback

SVC = "b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f"
RX = "b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f"
TX = "b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f"

# Devices advertise as ``BindDeck-XXXX`` (WS-267) or the legacy ``BindDeck``
# for a single un-upgraded deck. We discover everything with this prefix.
DEVICE_PREFIX = "BindDeck"
SCAN_TIMEOUT = 8.0
RECONNECT_DELAY = 3.0
# How often the manager re-scans for decks that have just been powered on /
# come into range, so newly added devices get connected without a restart.
DISCOVER_INTERVAL = 5.0

# bleak is imported lazily (see _load_bleak) rather than at module import time:
# importing it once the app's own imports have all settled avoids ordering
# clashes, and a failure is reported with a full traceback instead of being
# silently swallowed.
_BleakClient = None
_BleakScanner = None
_BleakGATTProtocolError = None
_BLEAK_ERROR = None
_BLEAK_TRACEBACK = None


def _load_bleak():
    """Import bleak on first use. Returns True when it is usable."""
    global _BleakClient, _BleakScanner, _BleakGATTProtocolError
    global _BLEAK_ERROR, _BLEAK_TRACEBACK
    if _BleakClient is not None:
        return True
    try:
        from bleak import BleakClient, BleakScanner
        from bleak.exc import BleakGATTProtocolError

        _BleakClient = BleakClient
        _BleakScanner = BleakScanner
        _BleakGATTProtocolError = BleakGATTProtocolError
        _BLEAK_ERROR = None
        _BLEAK_TRACEBACK = None
        return True
    except Exception as e:  # noqa: BLE001
        _BLEAK_ERROR = f"{type(e).__name__}: {e}"
        _BLEAK_TRACEBACK = traceback.format_exc()
        return False


def _is_security_error(err) -> bool:
    e = str(err).lower()
    return "nsum" in e or "uthentic" in e or "ncrypt" in e


def dev_id_from_name(name):
    """Turn a BLE advertisement name into the short device id.

    ``BindDeck-A1B2`` -> ``A1B2``; legacy ``BindDeck`` -> ``""`` (the single,
    un-upgraded deck, which every transport treats as the default device)."""
    if not name:
        return ""
    if not name.startswith(DEVICE_PREFIX):
        return ""
    suffix = name[len(DEVICE_PREFIX):]
    if suffix.startswith("-"):
        suffix = suffix[1:]
    return suffix


class BleLink:
    """One BLE connection to one physical BindDeck.

    Constructed by ``BleLinkManager`` with the device's address, id and name;
    owns a daemon thread running an asyncio loop that connects, subscribes to
    TX and drains an outbound command queue, reconnecting forever.
    """

    def __init__(self, address, dev_id, name, manager):
        self._address = address
        self.dev_id = dev_id
        self.name = name
        self._manager = manager
        self._loop = None
        self._queue = None
        self._connected = False
        self._lock = threading.Lock()

    # ---- thread-safe public API -------------------------------------------
    @property
    def available(self) -> bool:
        return _BleakClient is not None

    @property
    def connected(self) -> bool:
        with self._lock:
            return self._connected

    @property
    def address(self):
        with self._lock:
            return self._address

    def start(self) -> bool:
        """Start the per-device link thread. Returns False when bleak missing."""
        if not _load_bleak():
            return False
        threading.Thread(
            target=self._thread_main, daemon=True,
            name=f"ble-{self.dev_id or 'deck'}",
        ).start()
        return True

    def send(self, cmd: str) -> bool:
        """Queue one CFG:/CMD: line to *this* device. False when not connected."""
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
            print(f"[ble {self.dev_id or 'deck'}] fatal:", type(e).__name__, e)

    async def _runner(self):
        self._loop = asyncio.get_running_loop()
        self._queue = asyncio.Queue()
        while True:
            try:
                await self._session()
            except Exception as e:  # noqa: BLE001
                print(f"[ble {self.dev_id or 'deck'}] session error:", type(e).__name__, e)
            with self._lock:
                self._connected = False
            await asyncio.sleep(RECONNECT_DELAY)

    async def _connect(self):
        # This link is bound to a specific address discovered by the manager,
        # so just connect to it (a fresh scan is the manager's job).
        client = _BleakClient(self._address, timeout=20.0)
        try:
            await client.connect()
        except Exception as e:  # noqa: BLE001
            print(f"[ble {self.dev_id or 'deck'}] connect failed: {e}")
            try:
                await client.disconnect()
            except Exception:
                pass
            return None
        return client if client.is_connected else None

    async def _subscribe(self, client):
        def _tx(_c, data: bytearray):
            dev_id = self.dev_id
            for one in data.decode("utf-8", errors="ignore").splitlines():
                one = one.strip()
                if one:
                    self._manager._route(dev_id, one)

        try:
            await client.start_notify(TX, _tx)
        except _BleakGATTProtocolError as e:
            if not _is_security_error(e):
                raise
            print(f"[ble {self.dev_id or 'deck'}] notify needs security - pairing ...")
            await client.pair()
            await client.start_notify(TX, _tx)
        return _tx

    async def _write(self, client, cmd: str):
        try:
            await client.write_gatt_char(RX, cmd.encode(), response=True)
        except _BleakGATTProtocolError as e:
            if not _is_security_error(e):
                raise
            print(f"[ble {self.dev_id or 'deck'}] write needs security - pairing ...")
            await client.pair()
            await client.write_gatt_char(RX, cmd.encode(), response=True)

    async def _session(self):
        client = await self._connect()
        if client is None:
            return
        print(f"[ble {self.dev_id or 'deck'}] connected {client.address} mtu={client.mtu_size}")
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
                    print(f"[ble {self.dev_id or 'deck'}] write error:", e)
        finally:
            with self._lock:
                self._connected = False
            try:
                await client.disconnect()
            except Exception:
                pass
            print(f"[ble {self.dev_id or 'deck'}] disconnected")


class BleLinkManager:
    """Owns discovery and one ``BleLink`` per connected BindDeck.

    ``on_line(dev_id, line)`` is called for every decoded TX line, tagged with
    the source device's id so the app routes button presses and config
    correctly across multiple decks.
    """

    def __init__(self, on_line=None):
        self._on_line = on_line
        self._links = {}            # address   -> BleLink
        self._links_by_dev = {}     # dev_id    -> BleLink
        self._lock = threading.RLock()
        self._running = False

    # ---- properties used by the app --------------------------------------
    @property
    def available(self) -> bool:
        return _BleakClient is not None

    @property
    def error(self):
        return _BLEAK_ERROR

    @property
    def error_traceback(self):
        return _BLEAK_TRACEBACK

    @property
    def any_connected(self) -> bool:
        with self._lock:
            return any(l.connected for l in self._links.values())

    # ---- lifecycle --------------------------------------------------------
    def start(self) -> bool:
        """Load bleak and start the discovery thread. False when bleak missing."""
        if not _load_bleak():
            return False
        self._running = True
        threading.Thread(target=self._discovery_loop, daemon=True, name="ble-mgr").start()
        return True

    @staticmethod
    def _run_coro(coro):
        """Run a coroutine from synchronous code and return its result.

        ``BleakScanner.discover`` is a coroutine function, so it must be
        awaited. This call happens from the discovery thread and from Flask
        request handling - both synchronous - so we drive it with a fresh
        event loop. If the current thread already has a running loop (not
        expected here), run it in a throwaway thread instead of crashing with
        "asyncio.run() cannot be called from a running event loop"."""
        try:
            asyncio.get_running_loop()
        except RuntimeError:
            return asyncio.run(coro)

        result = {}

        def _worker():
            try:
                result["value"] = asyncio.run(coro)
            except Exception as e:  # noqa: BLE001
                result["error"] = e

        t = threading.Thread(target=_worker, daemon=True, name="ble-discover")
        t.start()
        t.join()
        if "error" in result:
            raise result["error"]
        return result.get("value")

    async def _discover_async(self, timeout):
        devices = await _BleakScanner.discover(timeout=timeout)
        found = []
        for d in devices:
            if d.name and d.name.startswith(DEVICE_PREFIX):
                found.append({
                    "dev_id": dev_id_from_name(d.name),
                    "name": d.name,
                    "address": d.address,
                })
        return found

    def discover(self, timeout=SCAN_TIMEOUT):
        """One-shot scan. Returns a list of dicts:
        ``{"dev_id", "name", "address"}`` for every BindDeck found."""
        if not _load_bleak():
            return []
        try:
            return self._run_coro(self._discover_async(timeout))
        except Exception as e:  # noqa: BLE001
            print("[ble] discover error:", e)
            return []

    def _discovery_loop(self):
        import time as _t
        while self._running:
            try:
                for d in self.discover():
                    self._ensure_link(d["address"], d["dev_id"], d["name"])
            except Exception as e:  # noqa: BLE001
                print("[ble] discovery loop error:", e)
            # Sleep in small slices so a stop is honoured quickly.
            for _ in range(int(DISCOVER_INTERVAL)):
                if not self._running:
                    return
                _t.sleep(1.0)

    def _ensure_link(self, address, dev_id, name):
        with self._lock:
            if address in self._links:
                return
            link = BleLink(address, dev_id, name, self)
            self._links[address] = link
            # A dev_id may collide if two decks somehow share a suffix; the
            # last-seen address wins for routing, which is fine in practice.
            self._links_by_dev[dev_id] = link
        link.start()

    # ---- inbound routing --------------------------------------------------
    def _route(self, dev_id, line):
        if self._on_line:
            try:
                self._on_line(dev_id, line)
            except Exception as e:  # noqa: BLE001
                print("[ble] line handler error:", e)

    # ---- outbound ---------------------------------------------------------
    def send(self, dev_id, cmd: str) -> bool:
        """Send a command to the single deck identified by ``dev_id``."""
        with self._lock:
            link = self._links_by_dev.get(dev_id)
        if link is None:
            return False
        return link.send(cmd)

    def send_all(self, cmd: str) -> bool:
        """Send a command to every connected deck. True if at least one got it."""
        ok = False
        with self._lock:
            links = list(self._links.values())
        for link in links:
            if link.send(cmd):
                ok = True
        return ok

    # ---- status -----------------------------------------------------------
    def devices(self):
        """Snapshot of every known deck for the UI / status endpoints."""
        with self._lock:
            return [
                {
                    "dev_id": l.dev_id,
                    "name": l.name,
                    "address": l.address,
                    "connected": l.connected,
                }
                for l in self._links.values()
            ]
