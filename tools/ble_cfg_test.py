#!/usr/bin/env python3
"""Drive / diagnose the BindDeck BLE config channel.

    service  b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f
    RX       b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f   write    (PC -> device)
    TX       b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f   notify   (device -> PC)

Every step prints something, so if it looks like "nothing happened" we can see
exactly where it stopped.

Install:  pip install bleak
Usage:
    python ble_cfg_test.py                          # scan, list, connect, send demo cmds
    python ble_cfg_test.py --scan                   # only scan and list devices
    python ble_cfg_test.py --addr AA:BB:CC:DD:EE:FF "CFG:ENC:1"
                                                    # skip scanning, connect by address
                                                    # (needed when the device is already
                                                    #  connected to Windows as a keyboard)

Getting the address on Windows, if the scan cannot see the device:
    Get-PnpDevice | ? {$_.FriendlyName -like '*BindDeck*'} | % InstanceId
    -> the "DEV_xxxxxxxxxxxx" part is the BLE address
"""
import argparse
import asyncio

from bleak import BleakClient, BleakScanner

SVC = "b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f"
RX = "b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f"
TX = "b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f"

DEVICE_NAME = "BindDeck"
SCAN_SECONDS = 10.0


async def scan() -> dict:
    print(f"[1/5] scanning {SCAN_SECONDS:.0f}s ...", flush=True)
    seen = {}

    def cb(dev, adv):
        seen[dev.address] = (dev.name, adv.rssi)

    async with BleakScanner(cb):
        await asyncio.sleep(SCAN_SECONDS)

    print(f"      {len(seen)} device(s) seen:", flush=True)
    for addr, (name, rssi) in sorted(seen.items()):
        print(f"        {addr}  rssi={rssi:>4}  {name}", flush=True)
    return seen


async def run(address: str, cmds: list) -> None:
    print(f"[3/5] connecting to {address} ...", flush=True)
    client = BleakClient(address, timeout=20.0)
    await client.connect()
    print(f"      connected={client.is_connected} mtu={client.mtu_size}", flush=True)
    if not client.is_connected:
        print("      connect FAILED", flush=True)
        return

    try:
        print("[4/5] services:", flush=True)
        uuids = []
        for s in client.services:
            flag = "  <-- config service" if s.uuid.lower() == SVC else ""
            print(f"        {s.uuid}{flag}", flush=True)
            uuids.append(s.uuid.lower())

        if SVC not in uuids:
            print("      config service NOT present - firmware did not create it", flush=True)
            return

        def on_tx(_c, data: bytearray) -> None:
            print(f"      <- {data.decode(errors='replace')}", flush=True)

        print(f"[5/5] subscribing TX + sending {len(cmds)} command(s)", flush=True)
        await client.start_notify(TX, on_tx)
        for cmd in cmds:
            print(f"      -> {cmd}", flush=True)
            await client.write_gatt_char(RX, cmd.encode(), response=True)
            await asyncio.sleep(0.6)
        print("      waiting 3s for notifications ...", flush=True)
        await asyncio.sleep(3.0)
        await client.stop_notify(TX)
    finally:
        await client.disconnect()
        print("done.", flush=True)


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", help="connect straight to this BLE address")
    ap.add_argument("--scan", action="store_true", help="only scan and list devices")
    ap.add_argument("cmds", nargs="*", help="CFG:/CMD: lines to send")
    args = ap.parse_args()

    cmds = args.cmds or ["CMD:GET_WIFI", "CFG:ENC:1"]

    if args.scan:
        await scan()
        return

    address = args.addr
    if not address:
        seen = await scan()
        print("[2/5] looking for the device ...", flush=True)
        address = next(
            (a for a, (n, _) in seen.items() if n and DEVICE_NAME.lower() in n.lower()),
            None,
        )
        if not address:
            print(f"      '{DEVICE_NAME}' was not advertising.", flush=True)
            print("      A bonded device that is already connected (as a keyboard)", flush=True)
            print("      normally stops advertising. Use --addr with its BLE address:", flush=True)
            print("        Get-PnpDevice | ? {$_.FriendlyName -like '*BindDeck*'} | % InstanceId", flush=True)
            return
        print(f"      found {address}", flush=True)

    await run(address, cmds)


if __name__ == "__main__":
    asyncio.run(main())
