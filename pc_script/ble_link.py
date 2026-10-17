"""BLE transport for BindDeck - CFG:/CMD: over a secondary GATT service.

The firmware exposes a vendor service next to HID on the same BLE connection:

    service  b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f
    RX       b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f   write    (PC -> device)
    TX       b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f   notify   (device -> PC)

Multiple BindDecks may be connected at once. Each device broadcasts a *unique*
BLE name ``BindDeck-XXXX`` (XXXX = last two MAC bytes); the side-channel
``BTN:<devId>:<btn>`` message carries that suffix so the PC can tell them
apart. ``BleLinkManager`` discovers every deck, connects each one, and tags
every incoming line with its device id:

    manager = BleLinkManager(on_line=handle_device_line)
    manager.start()                 # discovery + connections on one event loop
    manager.send(dev_id, "CFG:..")  # route a command to one device
    manager.send_all("CMD:..")      # broadcast to every connected deck

IMPORTANT: all BLE work (scan + every connection) runs on a *single* asyncio
loop on one daemon thread. An earlier design used one loop per device, which
is unreliable with bleak's Windows WinRT backend (COM apartment per thread)
and regressed discovery/connection. This single-loop model is the same shape
the original single-device link used, now generalised to N decks.

The firmware stops advertising while a central is connected on classic ESP32
(the C3 re-advertises). Discovery alone therefore cannot find an already
connected deck: we cache each deck's address (persisted to a JSON file) and
try the cached address directly, exactly like the original link did.

The characteristics are encrypted-only, so an unbonded client gets a security
error; we pair on demand and retry once.
"""

import asyncio
import json
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
    """Connection state + outbound queue for *one* physical BindDeck.

    It owns no thread: the manager's single event loop drives its connect /
    notify / write coroutine (`_device_session`). ``send`` is safe to call
    from any thread.
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


class BleLinkManager:
    """Owns discovery and one ``BleLink`` per BindDeck, on one event loop.

    ``on_line(dev_id, line)`` is called for every decoded TX line, tagged with
    the source device's id so the app routes button presses and config
    correctly across multiple decks.
    """

    def __init__(self, on_line=None, cache_path=None):
        self._on_line = on_line
        self._cache_path = cache_path
        self._links = {}            # address -> BleLink
        self._links_by_dev = {}     # dev_id  -> BleLink
        self._known = {}            # address -> {"dev_id","name"} (cache + scan)
        self._lock = threading.RLock()
        self._loop = None
        self._scan_lock = None
        self._running = False
        self._last_scan_sig = None
        self._load_cache()

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

    # ---- address cache ----------------------------------------------------
    def _load_cache(self):
        if not self._cache_path:
            return
        try:
            with open(self._cache_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            for item in data:
                addr = item.get("address")
                if addr:
                    self._known[addr] = {
                        "dev_id": item.get("dev_id", ""),
                        "name": item.get("name", ""),
                    }
        except FileNotFoundError:
            pass
        except Exception as e:  # noqa: BLE001
            print("[ble] cache load failed:", e)

    def _save_cache(self):
        if not self._cache_path:
            return
        try:
            with self._lock:
                items = [{"address": a, "dev_id": v.get("dev_id", ""), "name": v.get("name", "")}
                         for a, v in self._known.items()]
            with open(self._cache_path, "w", encoding="utf-8") as f:
                json.dump(items, f, indent=2)
        except Exception as e:  # noqa: BLE001
            print("[ble] cache save failed:", e)

    # ---- lifecycle --------------------------------------------------------
    def start(self) -> bool:
        """Load bleak and start the manager thread. False when bleak missing."""
        if not _load_bleak():
            return False
        self._running = True
        threading.Thread(target=self._thread_main, daemon=True, name="ble-mgr").start()
        return True

    def _thread_main(self):
        try:
            asyncio.run(self._runner())
        except Exception as e:  # noqa: BLE001
            print("[ble] fatal:", type(e).__name__, e)

    async def _runner(self):
        self._loop = asyncio.get_running_loop()
        self._scan_lock = asyncio.Lock()

        # First try every cached address directly - the deck may already be
        # connected to the OS via HID and therefore not advertising.
        for addr, info in list(self._known.items()):
            self._ensure_link(addr, info.get("dev_id", ""), info.get("name", ""))

        print("[ble] scanning for BindDeck* ...")
        first = True
        while self._running:
            try:
                await self._discover_and_connect(quick=first)
            except Exception as e:  # noqa: BLE001
                print("[ble] discovery loop error:", type(e).__name__, e)
            first = False
            await asyncio.sleep(DISCOVER_INTERVAL)

    # ---- discovery --------------------------------------------------------
    async def _scan(self, timeout):
        """Scan once (serialised). Returns [{dev_id,name,address}].

        Plain ``discover()`` is tried first: it is the longest-standing and
        most reliable path (and what the original single-device link used).
        Only if it returns nothing do we retry with ``return_adv=True`` so the
        advertised service UUID can identify a deck whose name wasn't
        resolved."""
        if self._scan_lock is None:
            self._scan_lock = asyncio.Lock()
        async with self._scan_lock:
            try:
                raw = await _BleakScanner.discover(timeout=timeout)
            except Exception as e:  # noqa: BLE001
                print("[ble] scan error:", type(e).__name__, e)
                raw = []

        items = self._normalise(raw)
        if not items:
            # Second attempt with advertisement data (service-UUID matching).
            async with self._scan_lock:
                try:
                    raw2 = await _BleakScanner.discover(timeout=timeout, return_adv=True)
                except Exception:  # noqa: BLE001
                    raw2 = None
            items = self._normalise(raw2)

        with self._lock:
            known = dict(self._known)

        found = []
        seen = set()
        for d, adv in items:
            addr = getattr(d, "address", None)
            if not addr or addr in seen:
                continue
            seen.add(addr)
            name = getattr(d, "name", None) or (getattr(adv, "local_name", None) if adv else None)
            services = list(getattr(adv, "service_uuids", None) or []) if adv else []
            cached = known.get(addr)
            if name and name.startswith(DEVICE_PREFIX):
                found.append({"dev_id": dev_id_from_name(name), "name": name, "address": addr})
            elif SVC in services:
                dev_id = dev_id_from_name(name) if name else (cached or {}).get("dev_id", "")
                found.append({"dev_id": dev_id,
                              "name": name or (cached or {}).get("name", "") or "BindDeck",
                              "address": addr})
            elif cached:
                # We connected to this address before; Windows sometimes fails
                # to resolve the name of an already-connected deck. Trust the
                # cache so the deck stays reachable.
                found.append({"dev_id": cached.get("dev_id", ""),
                              "name": cached.get("name", "") or "BindDeck",
                              "address": addr})

        self._log_scan(items, found)
        return found

    @staticmethod
    def _normalise(raw):
        """Normalise ``discover()`` output to a list of (BLEDevice, adv)."""
        if not raw:
            return []
        if isinstance(raw, dict):
            values = list(raw.values())
        else:
            values = list(raw)
        items = []
        for v in values:
            if isinstance(v, tuple) and len(v) == 2:
                items.append((v[0], v[1]))
            else:
                items.append((v, None))
        return items

    def _log_scan(self, items, found):
        """Throttled scan diagnostics, so the console shows whether BLE is
        seeing anything at all and why a deck was/wasn't matched."""
        raw = []
        for d, adv in items:
            name = getattr(d, "name", None) or (getattr(adv, "local_name", None) if adv else None) or "?"
            raw.append(f"{name}@{getattr(d, 'address', '?')}")
        sig = tuple(sorted(raw))
        if sig == self._last_scan_sig:
            return  # nothing changed since the last scan; stay quiet
        self._last_scan_sig = sig
        if found:
            print("[ble] scan found:", ", ".join(f"{r['name']}@{r['address']}" for r in found))
        elif raw:
            print(f"[ble] scan: {len(raw)} BLE device(s) seen, none is a BindDeck: "
                  + ", ".join(raw[:8]))
        else:
            print("[ble] scan: no BLE devices seen "
                  "(adapter off / not permitted / deck not advertising)")

    async def _discover_and_connect(self, quick=False):
        # A shorter first scan gives the user fast feedback at startup.
        timeout = min(SCAN_TIMEOUT, 4.0) if quick else SCAN_TIMEOUT
        found = await self._scan(timeout)
        for d in found:
            self._remember(d["address"], d["dev_id"], d["name"])
            self._ensure_link(d["address"], d["dev_id"], d["name"])

    def _remember(self, address, dev_id, name):
        with self._lock:
            prev = self._known.get(address, {})
            if prev.get("dev_id") == dev_id and prev.get("name") == name:
                return
            self._known[address] = {"dev_id": dev_id, "name": name}
        self._save_cache()

    def _ensure_link(self, address, dev_id, name):
        """Create a BleLink + its session coroutine if not already tracked.
        Called from the manager loop (or before it starts for cached ones)."""
        with self._lock:
            if address in self._links:
                return
            link = BleLink(address, dev_id, name, self)
            link._loop = self._loop
            self._links[address] = link
            self._links_by_dev[dev_id] = link
        print(f"[ble] deck {name or dev_id or address} ({address}) id={dev_id or '-'} -> connecting")
        asyncio.ensure_future(self._device_session(link))

    # ---- one device session ----------------------------------------------
    async def _device_session(self, link):
        tag = link.dev_id or "deck"
        while self._running:
            client = None
            try:
                client = _BleakClient(link.address, timeout=20.0)
                await client.connect()
                if not client.is_connected:
                    raise RuntimeError("connect returned without a live link")
                await self._subscribe(link, client)
                link._queue = asyncio.Queue()
                with link._lock:
                    link._connected = True
                print(f"[ble {tag}] connected {client.address} mtu={getattr(client, 'mtu_size', '?')}")
                while client.is_connected:
                    try:
                        cmd = await asyncio.wait_for(link._queue.get(), timeout=1.0)
                    except asyncio.TimeoutError:
                        continue
                    try:
                        await self._write(link, client, cmd)
                    except Exception as e:  # noqa: BLE001
                        print(f"[ble {tag}] write error:", e)
            except Exception as e:  # noqa: BLE001
                print(f"[ble {tag}] session error:", type(e).__name__, e)
            finally:
                with link._lock:
                    link._connected = False
                if client is not None:
                    try:
                        await client.disconnect()
                    except Exception:
                        pass
                print(f"[ble {tag}] disconnected")
            await asyncio.sleep(RECONNECT_DELAY)

    async def _subscribe(self, link, client):
        def _tx(_c, data: bytearray):
            for one in data.decode("utf-8", errors="ignore").splitlines():
                one = one.strip()
                if one:
                    self._route(link.dev_id, one)

        try:
            await client.start_notify(TX, _tx)
        except _BleakGATTProtocolError as e:
            if not _is_security_error(e):
                raise
            print(f"[ble {link.dev_id or 'deck'}] notify needs security - pairing ...")
            await client.pair()
            await client.start_notify(TX, _tx)
        return _tx

    async def _write(self, link, client, cmd: str):
        try:
            await client.write_gatt_char(RX, cmd.encode(), response=True)
        except _BleakGATTProtocolError as e:
            if not _is_security_error(e):
                raise
            print(f"[ble {link.dev_id or 'deck'}] write needs security - pairing ...")
            await client.pair()
            await client.write_gatt_char(RX, cmd.encode(), response=True)

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
        """Snapshot of every known deck for the UI / status endpoints.

        Includes decks seen only in a scan or from a cached address that we
        haven't connected to yet (connected=False), so the picker can list
        them and show why they aren't usable."""
        with self._lock:
            links = list(self._links.values())
            known = dict(self._known)
        by_key = {}
        order = []
        for l in links:
            key = l.dev_id or l.address
            cur = by_key.get(key)
            if cur is None or (l.connected and not cur.connected):
                if cur is None:
                    order.append(key)
                by_key[key] = l
        # Include cached/known decks that have no link yet.
        for addr, info in known.items():
            key = info.get("dev_id") or addr
            if key in by_key or addr in self._links:
                continue
            order.append(key)
            by_key[key] = {
                "dev_id": info.get("dev_id", ""),
                "name": info.get("name", ""),
                "address": addr,
                "connected": False,
            }
        out = []
        for key in order:
            v = by_key[key]
            if isinstance(v, BleLink):
                out.append({"dev_id": v.dev_id, "name": v.name, "address": v.address, "connected": v.connected})
            else:
                out.append(v)
        return out

    def discover(self, timeout=SCAN_TIMEOUT):
        """One-shot scan for synchronous callers. Runs on the manager loop when
        it is up so it never spins a competing asyncio loop (WinRT dislikes
        that); falls back to a throwaway loop before start()."""
        if not _load_bleak():
            return []
        loop = self._loop
        if loop is not None and loop.is_running():
            try:
                fut = asyncio.run_coroutine_threadsafe(self._scan(timeout), loop)
                return fut.result(timeout=timeout + 15)
            except Exception as e:  # noqa: BLE001
                print("[ble] discover error:", e)
                return []
        try:
            return asyncio.run(self._scan(timeout))
        except Exception as e:  # noqa: BLE001
            print("[ble] discover error:", e)
            return []
