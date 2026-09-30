#!/usr/bin/env python3
"""Drive the BindDeck BLE config channel (CFG:/CMD: lines over BLE).

The device exposes a secondary GATT service next to HID:

    service  b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f
    RX       b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f   write    (PC -> device)
    TX       b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f   notify   (device -> PC)

Write one CFG:/CMD: line per write (or several separated by '\\n'). Replies
arrive on TX as notifications while subscribed.

Install:  pip install bleak
Usage:    python ble_cfg_test.py                       # default demo commands
          python ble_cfg_test.py "CFG:BRIGHT:128" "CFG:ENC:1" "CMD:GET_WIFI"
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

SVC = "b1d3c0de-0001-4a5b-9c6d-1a2b3c4d5e6f"
RX = "b1d3c0de-0002-4a5b-9c6d-1a2b3c4d5e6f"
TX = "b1d3c0de-0003-4a5b-9c6d-1a2b3c4d5e6f"

DEVICE_NAME = "BindDeck"


async def main() -> None:
    cmds = sys.argv[1:] or ["CMD:GET_WIFI", "CFG:ENC:1", "CFG:BRIGHT:128"]

    print(f"scanning for '{DEVICE_NAME}' ...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15.0)
    if dev is None:
        print(f"'{DEVICE_NAME}' not found. Powered on and in range?")
        return
    print(f"found {dev.address} ({dev.name})")

    def on_tx(_char, data: bytearray) -> None:
        print(f"  <- {data.decode(errors='replace')}")

    async with BleakClient(dev) as client:
        print(f"connected (mtu={client.mtu_size})")

        services = [s.uuid for s in client.services]
        if SVC not in services:
            print("config service NOT found. Services present:")
            for s in services:
                print("   ", s)
            return

        await client.start_notify(TX, on_tx)
        try:
            for cmd in cmds:
                print(f"  -> {cmd}")
                await client.write_gatt_char(RX, cmd.encode(), response=True)
                await asyncio.sleep(0.5)
            print("waiting for notifications ...")
            await asyncio.sleep(2.0)
        finally:
            await client.stop_notify(TX)


if __name__ == "__main__":
    asyncio.run(main())
