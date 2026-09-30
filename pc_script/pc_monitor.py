import esptool

import subprocess
import sys
_old_popen = subprocess.Popen
def _new_popen(*args, **kwargs):
    if sys.platform == 'win32':
        kwargs['creationflags'] = getattr(subprocess, 'CREATE_NO_WINDOW', 0x08000000)
    return _old_popen(*args, **kwargs)
subprocess.Popen = _new_popen

import time
import tempfile
import re

import serial
import serial.tools.list_ports
import psutil
import requests
import threading
import json
import os
import sys
import socket
import urllib.request
import GPUtil
import random
from flask import Flask, render_template, request, jsonify

# BLE config channel (optional: without bleak the app still works over WiFi/USB)
_ble_import_error = None
_ble_import_tb = None
try:
    import ble_link
except Exception as _e:
    import traceback
    ble_link = None
    _ble_import_error = f"{type(_e).__name__}: {_e}"
    _ble_import_tb = traceback.format_exc()
ble_link_obj = None  # assigned once handle_device_line() exists

# keyboard and pywebview are in requirements.txt but are platform-specific
# (pywebview needs a GUI backend; keyboard needs OS-level hooks). Wrap them
# so a missing install degrades instead of crashing startup.
try:
    import keyboard
except ImportError:
    keyboard = None
try:
    import webview
except ImportError:
    webview = None

window_ref = None

# GUI / Icon dependencies — Windows-only. Wrapped so the app can still import
# (and run in degraded mode) when these are missing, e.g. on a non-Windows
# host or a partial pip install. Functions that need them import locally.
try:
    import win32gui
except ImportError:
    win32gui = None
try:
    import win32ui
except ImportError:
    win32ui = None
try:
    import win32con
except ImportError:
    win32con = None
try:
    import win32api
except ImportError:
    win32api = None
try:
    import win32com.client
except ImportError:
    win32com.client = None
try:
    from pycaw.pycaw import AudioUtilities, ISimpleAudioVolume
except ImportError:
    AudioUtilities = None
    ISimpleAudioVolume = None
try:
    import pystray
except ImportError:
    pystray = None
from PIL import Image
import base64
from io import BytesIO



def get_base_path():
    if getattr(sys, 'frozen', False):
        return sys._MEIPASS
    return os.path.dirname(os.path.abspath(__file__))

base_path = get_base_path()
app = Flask(__name__, 
            template_folder=os.path.join(base_path, 'templates'),
            static_folder=os.path.join(base_path, 'static'))

# Save config next to the executable, not in the temp MEIPASS dir
if getattr(sys, 'frozen', False):
    CONFIG_FILE = os.path.join(os.path.dirname(sys.executable), "macro_config.json")
else:
    CONFIG_FILE = os.path.join(base_path, "macro_config.json")

serial_port = None
# Last-known device IPv4, learned from the device's UDP packets on port 4211.
# Used (as a bonus unicast path) to send CFG/CMD back over Wi-Fi; the primary
# Wi-Fi path is a broadcast to 255.255.255.255:4210, which the device already
# listens on (see firmware loopWiFi -> processCommand).
device_ip = None
config = {
    "keys": {str(i): {"type": "none", "value": "", "anim": -1} for i in range(13, 22)},
    "esp32": {"animMode": 0, "encMode": 0, "pins": [13, 12, 14, 27, 32, 33, 25, 26], "sleepEnabled": True, "sleepTimeout": 5},
    "app": {"theme": "dark", "lang": "en", "startup": False, "closeMode": "ask", "deviceType": "esp32", "connection_type": "usb"}
}

# --- OTA UPDATER ---
# Firmware (ESP32) update state
UPDATE_AVAILABLE = False
NEW_VERSION_URL = ""
NEW_VERSION_NAME = ""

# Desktop app (.exe) self-update state
APP_UPDATE_AVAILABLE = False
APP_UPDATE_URL = ""
APP_UPDATE_VERSION = ""

# TODO: THE USER MUST CHANGE THIS TO THEIR REAL REPO (E.g., "SanX18/BindDeck")
GITHUB_REPO = "SanX18/BindDeck"
CURRENT_VERSION = "V1.0.0.8"

# Name of the app executable asset expected on each GitHub Release, alongside firmware.bin
APP_ASSET_NAME = "BindDeck.exe"

def fetch_latest_release():
    url = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, timeout=10) as response:
        return json.loads(response.read().decode())

def parse_version_tuple(version_str):
    """Pulls the numeric x.y.z(.w) part out of a version/tag string, ignoring
    any prefix, so tags like 'BindDeck_V1.0.0.5' and 'V1.0.0.5' compare equal
    instead of looking like different versions just because of the prefix."""
    match = re.search(r'(\d+(?:\.\d+)+)', version_str or "")
    if not match:
        return None
    return tuple(int(part) for part in match.group(1).split('.'))

def is_newer_version(remote_tag, current):
    remote_v = parse_version_tuple(remote_tag)
    current_v = parse_version_tuple(current)
    if remote_v is None or current_v is None:
        # Can't parse either as a version number, fall back to a plain diff
        # so we still surface unexpected tags instead of silently ignoring them.
        return bool(remote_tag) and remote_tag != current
    return remote_v > current_v

def apply_release_data(data):
    """Checks a GitHub release payload for newer firmware and/or app assets
    and updates the matching global update-state flags. Returns True if the
    release is newer than CURRENT_VERSION (regardless of which assets exist)."""
    global UPDATE_AVAILABLE, NEW_VERSION_URL, NEW_VERSION_NAME
    global APP_UPDATE_AVAILABLE, APP_UPDATE_URL, APP_UPDATE_VERSION

    latest_version = data.get("tag_name", "")
    if not is_newer_version(latest_version, CURRENT_VERSION):
        return False

    for asset in data.get("assets", []):
        name = asset.get("name", "")
        if name == "firmware.bin":
            NEW_VERSION_URL = asset.get("browser_download_url")
            NEW_VERSION_NAME = latest_version
            UPDATE_AVAILABLE = True
        elif name.lower() == APP_ASSET_NAME.lower():
            APP_UPDATE_URL = asset.get("browser_download_url")
            APP_UPDATE_VERSION = latest_version
            APP_UPDATE_AVAILABLE = True

    return True

def check_for_updates():
    while True:
        try:
            apply_release_data(fetch_latest_release())
        except Exception:
            pass
        time.sleep(86400) # Check once a day

threading.Thread(target=check_for_updates, daemon=True).start()

# --- CONFIG MANAGEMENT ---
def _deep_merge(base, override):
    """Recursively merge override into base. Keys missing from override are
    kept from base, so a stale macro_config.json (e.g. one saved by an
    older version without esp32.pins) does not wipe defaults like pins."""
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(base.get(k), dict):
            _deep_merge(base[k], v)
        else:
            base[k] = v
    return base

def load_config():
    global config
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, 'r') as f:
                loaded = json.load(f)
                _deep_merge(config, loaded)
        except Exception as e:
            print("Error loading config:", e)
    print(f"[DBG load_config] esp32.pins = {config.get('esp32', {}).get('pins')!r}")

def manage_startup(enable):
    try:
        startup_path = os.path.join(os.environ["APPDATA"], r"Microsoft\Windows\Start Menu\Programs\Startup", "BindDeck.bat")
        if enable:
            exe_path = sys.executable if getattr(sys, 'frozen', False) else os.path.abspath(__file__)
            cmd = f'start "" "{exe_path}"' if getattr(sys, 'frozen', False) else f'start "" pythonw "{exe_path}"'
            with open(startup_path, 'w') as f:
                f.write(f"@echo off\n{cmd}\n")
        else:
            if os.path.exists(startup_path):
                os.remove(startup_path)
    except Exception as e:
        print("Error in startup:", e)

def save_config():
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=4)
        manage_startup(config.get("app", {}).get("startup", False))
    except Exception as e:
        print("Error saving config:", e)

# --- DEVICE PROFILES (per chip) ---
# default_pins: the 8 button GPIOs in order.
# allowed_pins: when not None, these are the ONLY GPIOs the chip permits for
#   buttons (mirrors the firmware's isValidButtonPin for that chip). When None,
#   any 0-39 GPIO is allowed except those in `reserved`.
# reserved: GPIOs the device cannot use for buttons (OLED, encoder, UART, and on
#   C3 the embedded-flash lines 11-17 which would hard-crash the chip).
# The ESP32-C3 has no usable 11-17 (flash) and 18/19 are USB-CDC, 20/21 are the
# encoder, 1 is the shared menu/encoder-press, 4/5 are the OLED.
DEVICE_PROFILES = {
    "esp32": {
        "label": "ESP32",
        "default_pins": [13, 12, 14, 27, 32, 33, 25, 26],
        "allowed_pins": None,
        "reserved": {4, 5, 18, 19, 21, 22},
        "hint": "Reserved: 4, 5 (OLED), 18, 19 (UART), 21 (encoder DT), 22 (encoder CLK).",
    },
    "esp32c3": {
        "label": "ESP32-C3",
        "default_pins": [0, 2, 3, 6, 7, 8, 9, 10],
        "allowed_pins": {0, 2, 3, 6, 7, 8, 9, 10},  # mirrors firmware isValidButtonPin
        "reserved": {1, 4, 5, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21},
        "hint": "Reserved: 1 (menu/press), 4,5 (OLED), 20,21 (encoder), 11-17 (embedded flash — DO NOT USE).",
    },
}

def get_device_type():
    return config.get("app", {}).get("deviceType", "esp32")

def get_profile(device_type=None):
    if device_type is None:
        device_type = get_device_type()
    return DEVICE_PROFILES.get(device_type, DEVICE_PROFILES["esp32"])

_RESERVED_PINS = DEVICE_PROFILES["esp32"]["reserved"]  # legacy alias

def _valid_pin_set(pins, device_type=None):
    """Validate a button GPIO assignment before sending it to the device."""
    prof = get_profile(device_type)
    if not isinstance(pins, list) or len(pins) != 8:
        return False
    seen = set()
    for p in pins:
        try:
            p = int(p)
        except (TypeError, ValueError):
            return False
        if prof["allowed_pins"] is not None:
            if p not in prof["allowed_pins"]:
                return False
        else:
            if p < 0 or p > 39 or p in prof["reserved"]:
                return False
        if p in seen:
            return False
        seen.add(p)
    return True

def send_to_device(cmd):
    """Send a CFG:/CMD: line to the device.

    Primary path is a Wi-Fi broadcast to 255.255.255.255:4210 (plus each
    subnet's directed broadcast) — the device already listens on UDP 4210 and
    runs processCommand() on every received packet, so this reaches the C3
    without needing its IP (the C3 has no USB serial). A unicast to the last
    known device_ip is added when available, and USB serial is used when open
    (classic ESP32). Returns True if the command left through at least one
    channel."""
    payload = (cmd + "\n").encode('utf-8')
    sent = False
    try:
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_socket.sendto(payload, ('255.255.255.255', 4210))
        sent = True
    except Exception as e:
        print("UDP broadcast send error:", e)
    for bcast in get_broadcast_ips():
        try:
            udp_socket.sendto(payload, (bcast, 4210))
            sent = True
        except Exception:
            pass
    if device_ip:
        try:
            udp_socket.sendto(payload, (device_ip, 4210))
            sent = True
        except Exception:
            pass
    if serial_port and serial_port.is_open:
        try:
            serial_port.write(payload)
            sent = True
        except Exception as e:
            print("Serial send error:", e)
    # BLE config channel: the same CFG:/CMD: lines over the existing BLE link,
    # so the device can be configured with no WiFi at all.
    if ble_link_obj is not None and ble_link_obj.send(cmd):
        sent = True
    return sent


def _kbd_send(seq):
    if keyboard is not None:
        keyboard.send(seq)


# Last firmware identity reported by the device (CMD:VERSION).
fw_info = {"version": "", "display": "", "chip": "", "build": ""}


def handle_device_line(line):
    """Handle one line coming back from the device.

    USB serial, WiFi UDP and BLE notifications all funnel through here, so the
    behaviour is identical whichever transport the line arrived on.
    """
    if line.startswith("BTN:"):
        try:
            idx = int(line.split(":")[1])
            threading.Thread(target=execute_macro, args=(idx + 13,), daemon=True).start()
        except Exception:
            pass
    elif line.startswith("ENC:"):
        cmd = line.split(":")[1] if ":" in line else ""
        if cmd in ("APPVUP", "APPVDN"):
            app_name = config.get("esp32", {}).get("encApp", "")
            if app_name:
                threading.Thread(target=change_app_volume,
                                 args=(app_name, cmd == "APPVUP"), daemon=True).start()
        elif cmd == "VUP": _kbd_send("volume up")
        elif cmd == "VDN": _kbd_send("volume down")
        elif cmd == "ZIN": _kbd_send("ctrl++")
        elif cmd == "ZOUT": _kbd_send("ctrl+-")
        elif cmd == "TFWD": _kbd_send("ctrl+tab")
        elif cmd == "TBCK": _kbd_send("ctrl+shift+tab")
        elif cmd == "REDO": _kbd_send("ctrl+y")
        elif cmd == "UNDO": _kbd_send("ctrl+z")
    elif line.startswith("WIFI_INFO:"):
        try:
            parts = line[len("WIFI_INFO:"):].split(",")
            wifi_status_data["connected"] = True
            if len(parts) > 0: wifi_status_data["ssid"] = parts[0]
            if len(parts) > 1: wifi_status_data["ip"] = parts[1]
        except Exception:
            pass
    elif line.startswith("VERSION:"):
        # VERSION:<fw>,<display>,<chip>,<build> - identifies which variant the
        # board is running, so the UI can warn about a wrong image.
        try:
            parts = line[len("VERSION:"):].split(",")
            fw_info["version"] = parts[0].strip() if len(parts) > 0 else ""
            fw_info["display"] = parts[1].strip() if len(parts) > 1 else ""
            fw_info["chip"]    = parts[2].strip() if len(parts) > 2 else ""
            fw_info["build"]   = parts[3].strip() if len(parts) > 3 else ""
        except Exception:
            pass
    # ACK:<cmd> lines just confirm a CFG: command was applied; nothing to do.


ble_link_obj = ble_link.BleLink(on_line=handle_device_line) if ble_link else None

# --- KEYBOARD HOOKS ---
last_macro_times = {}

def execute_macro(key_index):
    global last_macro_times
    now = time.time()
    if now - last_macro_times.get(key_index, 0) < 0.3:
        return
    last_macro_times[key_index] = now
    
    if key_index == 21:
        toggle_audio()
        return
        
    action = config["keys"].get(str(key_index), {})
    action_type = action.get("type", "none")
    value = action.get("value", "")
    anim = action.get("anim", -1)
    
    try:
        if int(anim) != -1 and window_ref:
            window_ref.evaluate_js(f"if(typeof playOledPreview === 'function') playOledPreview({anim}, true);")
    except Exception as e:
        print(f"Error anim preview: {e}")
    
    if action_type == "app" and value:
        try:
            val = value.strip('"').strip("'")
            os.startfile(val)
        except Exception as e:
            try:
                subprocess.Popen(value, shell=True)
            except Exception as e2:
                print(f"Error abriendo app: {e2}")
    elif action_type == "shortcut" and value:
        try:
            keys = value.split('+')
            for k in keys: keyboard.press(k)
            time.sleep(0.05)
            for k in reversed(keys): keyboard.release(k)
        except Exception as e:
            print(f"Error shortcut: {e}")
    elif action_type == "text" and value:
        try:
            keyboard.write(value)
        except Exception as e:
            pass
    elif action_type == "none":
        try:
            keyboard.press(f"f{key_index}")
            time.sleep(0.05)
            keyboard.release(f"f{key_index}")
        except Exception as e:
            print(f"Error none: {e}")


def change_app_volume(app_name, up):
    try:
        from pycaw.pycaw import AudioUtilities, ISimpleAudioVolume, IAudioEndpointVolume
        from ctypes import cast, POINTER
        from comtypes import CLSCTX_ALL
        
        if app_name.lower() in ["system", "system volume", "sistema"]:
            devices = AudioUtilities.GetSpeakers()
            interface = devices.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
            volume = cast(interface, POINTER(IAudioEndpointVolume))
            current_vol = volume.GetMasterVolumeLevelScalar()
            if up:
                new_vol = min(1.0, current_vol + 0.04)
            else:
                new_vol = max(0.0, current_vol - 0.04)
            volume.SetMasterVolumeLevelScalar(new_vol, None)
            return

        sessions = AudioUtilities.GetAllSessions()
        for session in sessions:
            if session.Process and session.Process.name() and session.Process.name().lower() == app_name.lower():
                volume = session._ctl.QueryInterface(ISimpleAudioVolume)
                current_vol = volume.GetMasterVolume()
                if up:
                    new_vol = min(1.0, current_vol + 0.04)
                else:
                    new_vol = max(0.0, current_vol - 0.04)
                volume.SetMasterVolume(new_vol, None)
    except Exception as e:
        print("Error changing app volume:", e)

current_audio_toggle = 0
def toggle_audio():
    global current_audio_toggle
    # Use SoundVolumeView Device Names for precise switching
    devices = [
        ("Hi-MAX", "Hi-MAX"),
        ("G435 Wireless Gaming Headset", "G435")
    ]
    current_audio_toggle = (current_audio_toggle + 1) % len(devices)
    dev = devices[current_audio_toggle]
    
    try:
        svv_path = os.path.join(sys._MEIPASS, "SoundVolumeView.exe") if getattr(sys, 'frozen', False) else os.path.abspath(os.path.join(os.path.dirname(__file__), "SoundVolumeView.exe"))
        subprocess.run([svv_path, "/SetDefault", dev[0], "all"], creationflags=subprocess.CREATE_NO_WINDOW)
        
        if serial_port and serial_port.is_open:
            serial_port.write(f"CMD:MSG:{dev[1]}\n".encode())
    except Exception as e:
        print("Error toggling audio:", e)

def on_key_event(e):
    if e.event_type == keyboard.KEY_DOWN:
        if e.name.startswith('f') and e.name[1:].isdigit():
            key_num = int(e.name[1:])
            if key_num == 23 or key_num == 24:
                if config.get("esp32", {}).get("encMode", 0) == 5:
                    app_name = config.get("esp32", {}).get("encApp", "")
                    if app_name:
                        threading.Thread(target=change_app_volume, args=(app_name, key_num == 24), daemon=True).start()
                    return False
            if key_num == 21: # Encoder button toggle audio
                threading.Thread(target=toggle_audio, daemon=True).start()
                return False
            if 13 <= key_num <= 20: 
                action_type = config["keys"].get(str(key_num), {}).get("type", "none")
                if action_type != "none":
                    threading.Thread(target=execute_macro, args=(key_num,), daemon=True).start()
                    return False
    return True

# --- FLASK API ---

@app.after_request
def add_header(response):
    response.headers['Cache-Control'] = 'no-store, no-cache, must-revalidate, post-check=0, pre-check=0, max-age=0'
    response.headers['Pragma'] = 'no-cache'
    response.headers['Expires'] = '-1'
    return response

@app.route('/')

def index():
    return render_template('index.html')

@app.route('/api/flash_bundled', methods=['POST'])
def api_flash_bundled():
    port_to_flash = find_esp32_port()
    if not port_to_flash:
        return jsonify({"success": False, "error": "Device not connected via USB"})
        
    global serial_port
    if serial_port and serial_port.is_open:
        try:
            serial_port.write(b"CMD:UPDATE\n")
            serial_port.flush()
            time.sleep(1) # wait for OLED to render the updating screen
        except:
            pass
        serial_port.close()
        serial_port = None
        
    fw_path = os.path.join(base_path, 'firmware.bin')
    if not os.path.exists(fw_path):
        return jsonify({"success": False, "error": "Bundled firmware not found"})
        
    try:
        import subprocess
        try:
            result = subprocess.run(
                ["python", "-m", "esptool", "--port", port_to_flash, "--baud", "460800", "write_flash", "-z", "0x10000", fw_path],
                capture_output=True, text=True, check=True
            )
        except subprocess.CalledProcessError as e:
            return jsonify({"success": False, "error": f"esptool failed: {e.stderr}"})
        return jsonify({"success": True})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)})


@app.route('/api/flash_local', methods=['POST'])
def api_flash_local():
    if 'file' not in request.files:
        return jsonify({"success": False, "error": "No file uploaded"})
    
    file = request.files['file']
    if file.filename == '':
        return jsonify({"success": False, "error": "No file selected"})
        
    port_to_flash = find_esp32_port()
    if not port_to_flash:
        return jsonify({"success": False, "error": "Device not connected via USB"})
        
    global serial_port
    if serial_port and serial_port.is_open:
        serial_port.close()
        serial_port = None
        
    try:
        file.save("local_firmware.bin")
        cmd = [sys.executable, "-m", "esptool", "--port", port_to_flash, "--baud", "460800", "write_flash", "-z", "0x10000", "local_firmware.bin"]
        subprocess.run(cmd, check=True)
        if os.path.exists("local_firmware.bin"):
            os.remove("local_firmware.bin")
        return jsonify({"success": True})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)})


@app.route('/api/sync_pins', methods=['POST'])
def api_sync_pins():
    """Force-send the current button GPIO set to the device, regardless of
    whether it differs from the last-known state. Useful after a config
    restore or when the device may have missed a CFG:PINS: earlier."""
    pins = config.get("esp32", {}).get("pins")
    if pins and _valid_pin_set(pins):
        pins_str = ",".join(str(int(p)) for p in pins)
        try:
            ok = send_to_device(f"CFG:PINS:{pins_str}")
            print(f"[DBG CFG:PINS] FORCE SENT -> CFG:PINS:{pins_str} ok={ok}")
            return jsonify({"success": ok})
        except Exception as e:
            print("Error sending CFG:PINS:", e)
    return jsonify({"success": False})

@app.route('/api/config', methods=['GET', 'POST'])
def api_config():
    global config
    if request.method == 'POST':
        new_cfg = request.json
        old_esp32 = config.get("esp32", {})
        old_keys = config.get("keys", {})
        config = new_cfg
        save_config()
        
        # Send over USB serial when open, over Wi-Fi when a device IP is known,
        # or unconditionally when the connection is Wi-Fi (the device receives
        # the broadcast on UDP 4210 even before it has reported its IP back).
        if serial_port and serial_port.is_open or device_ip or \
           new_cfg.get("app", {}).get("connection_type") == "wifi":
            try:
                new_esp32 = new_cfg.get("esp32", {})
                new_keys = new_cfg.get("keys", {})
                
                anim = new_esp32.get("animMode", 0)
                if anim is None: anim = 0
                if str(old_esp32.get("animMode", "")) != str(anim):
                    send_to_device(f"CFG:ANIM:{anim}")
                    time.sleep(0.1)

                enc = new_esp32.get("encMode", 0)
                if enc is None: enc = 0
                if str(old_esp32.get("encMode", "")) != str(enc):
                    send_to_device(f"CFG:ENC:{enc}")
                    time.sleep(0.1)

                brt = new_esp32.get("brightness", 255)
                if brt is None: brt = 255
                if str(old_esp32.get("brightness", "")) != str(brt):
                    send_to_device(f"CFG:BRIGHT:{brt}")
                    time.sleep(0.1)

                # Sleep mode: CFG:SLEEP:<enabled>,<timeout_minutes>
                sleep_enabled = new_esp32.get("sleepEnabled", True)
                sleep_timeout = new_esp32.get("sleepTimeout", 5)
                if str(old_esp32.get("sleepEnabled", "")) != str(sleep_enabled) or \
                   str(old_esp32.get("sleepTimeout", "")) != str(sleep_timeout):
                    send_to_device(f"CFG:SLEEP:{int(bool(sleep_enabled))},{int(sleep_timeout)}")
                    print(f"[DBG CFG:SLEEP] SENT -> enabled={sleep_enabled} timeout={sleep_timeout}m")
                    time.sleep(0.1)

                # Button GPIOs — only send when the pin set actually changed
                new_pins = new_esp32.get("pins")
                old_pins = old_esp32.get("pins")
                print(f"[DBG CFG:PINS] old_pins={old_pins!r} new_pins={new_pins!r}")
                if new_pins and old_pins and str(new_pins) != str(old_pins):
                    if _valid_pin_set(new_pins):
                        pins_str = ",".join(str(int(p)) for p in new_pins)
                        send_to_device(f"CFG:PINS:{pins_str}")
                        print(f"[DBG CFG:PINS] SENT -> CFG:PINS:{pins_str}")
                        time.sleep(0.1)
                    else:
                        print(f"[DBG CFG:PINS] REJECTED by _valid_pin_set: {new_pins!r}")

                # Check if key animations changed
                changed_anims = False
                kb_anims = []
                for i in range(13, 22):
                    key_anim = new_keys.get(str(i), {}).get("anim", -1)
                    old_anim = old_keys.get(str(i), {}).get("anim", -1)
                    if str(key_anim) != str(old_anim): changed_anims = True
                    kb_anims.append(str(key_anim))
                
                if changed_anims:
                    kb_anims_str = ",".join(kb_anims)
                    send_to_device(f"CFG:KB_ANIM:{kb_anims_str}")
                    time.sleep(0.1)
                
                # Check if key texts changed
                for i in range(13, 22):
                    disp_text = new_keys.get(str(i), {}).get("dispText", "")
                    old_text = old_keys.get(str(i), {}).get("dispText", "")
                    if str(disp_text) != str(old_text):
                        idx = i - 13
                        send_to_device(f"CFG:TXT:{idx}:{disp_text}")
                        time.sleep(0.1)
                
            except Exception as e:
                print("Error enviando config al ESP32:", e)
                
        return jsonify({"status": "success"})
    return jsonify(config)

@app.route("/api/preview/<int:mode>", methods=["GET"])
def api_preview(mode):
    send_to_device(f"CMD:PREVIEW:{mode}")
    return jsonify({"success": True})

wifi_status_data = {"connected": False, "ssid": "", "ip": ""}

@app.route("/api/get_wifi_status", methods=["GET"])
def api_get_wifi_status():
    global wifi_status_data
    if serial_port and serial_port.is_open or device_ip or \
       (ble_link_obj is not None and ble_link_obj.connected) or \
       config.get("app", {}).get("connection_type") == "wifi":
        try:
            send_to_device("CMD:GET_WIFI")
            time.sleep(0.5)
            return jsonify(wifi_status_data)
        except:
            pass
    return jsonify({"connected": False, "ssid": "", "ip": ""})


@app.route("/api/ble_status", methods=["GET"])
def api_ble_status():
    """State of the BLE config channel (used by the connection panel)."""
    if ble_link_obj is None or not ble_link_obj.available:
        return jsonify({"available": False, "connected": False, "address": None})
    return jsonify({
        "available": True,
        "connected": ble_link_obj.connected,
        "address": ble_link_obj.address,
    })


@app.route("/api/fw_version", methods=["GET"])
def api_fw_version():
    """Ask the device which firmware it is running.

    Returns version/display/chip/build so the UI can show it and warn when the
    flashed image does not match the selected board variant.
    """
    send_to_device("CMD:VERSION")
    time.sleep(0.5)
    return jsonify(fw_info)

@app.route("/api/send_config", methods=["POST"])
def api_send_config():
    data = request.json
    cmd = data.get("cmd", "")
    if cmd:
        send_to_device(cmd)
    return jsonify({"success": True})

@app.route('/api/simulate', methods=['POST'])
def api_simulate():
    try:
        data = request.json
        val = int(data.get("id", 0))
        print(f"SIMULATING {val}")
        send_to_device(f"CMD:SIMULATE:{val}")
    except Exception as e:
        print(f"Error simulate: {e}")
    return jsonify({"status": "ok"})

SIMULATE_TEMP = False

@app.route('/api/simulate_temp', methods=['POST'])
def api_simulate_temp():
    global SIMULATE_TEMP
    SIMULATE_TEMP = not SIMULATE_TEMP
    return jsonify({"status": "ok", "simulate": SIMULATE_TEMP})


def get_icon_base64(path):
    try:
        import win32gui, win32ui, win32con, win32api
        from PIL import Image
        import base64
        from io import BytesIO
        
        large, small = win32gui.ExtractIconEx(path, 0)
        if not large and not small: return ""
        hicon = large[0] if large else small[0]
        
        ico_x = win32api.GetSystemMetrics(win32con.SM_CXICON)
        ico_y = win32api.GetSystemMetrics(win32con.SM_CYICON)
        
        hdc = win32ui.CreateDCFromHandle(win32gui.GetDC(0))
        mdc = hdc.CreateCompatibleDC()
        hbmp = win32ui.CreateBitmap()
        hbmp.CreateCompatibleBitmap(hdc, ico_x, ico_y)
        mdc.SelectObject(hbmp)
        
        brush = win32ui.CreateBrush(win32con.BS_SOLID, win32api.RGB(30, 30, 30), 0)
        mdc.FillRect((0, 0, ico_x, ico_y), brush)
        win32gui.DrawIconEx(mdc.GetSafeHdc(), 0, 0, hicon, ico_x, ico_y, 0, None, win32con.DI_NORMAL)
        
        bmpinfo = hbmp.GetInfo()
        bmpstr = hbmp.GetBitmapBits(True)
        img = Image.frombuffer('RGB', (bmpinfo['bmWidth'], bmpinfo['bmHeight']), bmpstr, 'raw', 'BGRX', 0, 1)
        
        win32gui.DestroyIcon(hicon)
        
        buffered = BytesIO()
        img.save(buffered, format="PNG")
        return "data:image/png;base64," + base64.b64encode(buffered.getvalue()).decode()
    except Exception as e:
        return ""

@app.route('/api/installed_apps')
def api_installed_apps():
    import psutil, win32com.client
    shell = win32com.client.Dispatch('WScript.Shell')
    paths = [
        os.environ.get('PROGRAMDATA', 'C:\\ProgramData') + r'\Microsoft\Windows\Start Menu\Programs',
        os.environ.get('APPDATA') + r'\Microsoft\Windows\Start Menu\Programs'
    ]
    apps = [{'name': 'System Volume', 'path': 'System', 'exe': 'System', 'icon': ''}]
    seen_names = set()
    seen_exes = set(['system'])
    try:
        for p in paths:
            for root, dirs, files in os.walk(p):
                for file in files:
                    if file.endswith('.lnk'):
                        try:
                            shortcut = shell.CreateShortCut(os.path.join(root, file))
                            target = shortcut.Targetpath
                            if target.lower().endswith('.exe'):
                                name = os.path.splitext(file)[0]
                                exe = os.path.basename(target)
                                if name not in seen_names and exe.lower() not in ['update.exe', 'unins000.exe', 'uninstall.exe']:
                                    apps.append({'name': name, 'path': target, 'exe': exe, 'icon': get_icon_base64(target)})
                                    seen_names.add(name)
                                    seen_exes.add(exe.lower())
                        except: pass
        for p in psutil.process_iter(['name', 'exe']):
            try:
                exe_path = p.info['exe']
                exe_name = p.info['name']
                if exe_path and exe_name.endswith('.exe') and exe_name.lower() not in seen_exes:
                    if 'system32' not in exe_path.lower() and 'windowsapps' not in exe_path.lower():
                        name = os.path.splitext(exe_name)[0].capitalize()
                        apps.append({'name': name + ' (Running)', 'path': exe_path, 'exe': exe_name, 'icon': get_icon_base64(exe_path)})
                        seen_exes.add(exe_name.lower())
            except: pass
        apps.sort(key=lambda x: x['name'])
    except Exception as e:
        print("Error in apps:", e)
    return jsonify(apps)

@app.route('/api/audio_apps')
def api_audio_apps():
    apps = [{"name": "System Volume (Master)", "exe": "System", "icon": ""}]
    try:
        from pycaw.pycaw import AudioUtilities
        sessions = AudioUtilities.GetAllSessions()
        seen = set()
        for session in sessions:
            if session.Process and session.Process.name():
                name = session.Process.name()
                if name.lower() not in seen:
                    seen.add(name.lower())
                    apps.append({"name": name, "exe": name, "icon": ""})
    except Exception as e:
        print("Error getting audio apps:", e)
    return jsonify(apps)

@app.route('/api/status')
def api_status():
    conn_type = config.get("app", {}).get("connection_type", "usb")
    
    is_usb = serial_port is not None and serial_port.is_open
    is_bt = getattr(app, 'bt_connected', False)
    is_wifi = device_ip is not None
    
    if is_usb:
        return jsonify({"connected": True, "type": "USB", "ping": random.randint(8, 24), "battery": None})
    elif is_bt:
        return jsonify({"connected": True, "type": "Bluetooth", "ping": random.randint(30, 85), "battery": getattr(app, 'bt_battery', None)})
    elif is_wifi:
        return jsonify({"connected": True, "type": "Wi-Fi", "ping": random.randint(2, 12), "battery": None})
    else:
        return jsonify({"connected": False, "type": "None", "ping": 0, "battery": None})

@app.route('/api/device_profile', methods=['GET'])
def api_device_profile():
    """Return the active device profile so the UI can show correct default
    pins, allowed/ reserved GPIOs and hint text for the selected chip."""
    dt = get_device_type()
    prof = get_profile(dt)
    return jsonify({
        "deviceType": dt,
        "label": prof["label"],
        "default_pins": prof["default_pins"],
        "allowed_pins": sorted(prof["allowed_pins"]) if prof["allowed_pins"] is not None else None,
        "reserved": sorted(prof["reserved"]),
        "hint": prof["hint"],
        "available": list(DEVICE_PROFILES.keys()),
    })

def check_bt_status_loop():
    ps_cmd = "$dev = Get-PnpDevice -Class Bluetooth | Where-Object { $_.FriendlyName -match 'BindDeck' -and $_.Status -eq 'OK' }; if ($dev) { Write-Output 'CONNECTED'; $prop = Get-PnpDeviceProperty -InstanceId $dev.InstanceId -KeyName '{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2' -ErrorAction SilentlyContinue; if ($prop -and $prop.Data -ne $null) { Write-Output $prop.Data } }"
    while True:
        try:
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            output = subprocess.check_output(
                ["powershell", "-NoProfile", "-Command", ps_cmd],
                startupinfo=startupinfo,
                creationflags=0x08000000,
                text=True
            ).strip().split('\n')
            
            output = [line.strip() for line in output if line.strip()]
            
            app.bt_connected = len(output) > 0 and output[0] == "CONNECTED"
            if len(output) > 1 and output[1].isdigit():
                app.bt_battery = int(output[1])
            else:
                app.bt_battery = None
        except Exception:
            app.bt_connected = False
            app.bt_battery = None
        # Spawning powershell.exe is expensive (cold start), and BT connection
        # state doesn't need sub-5s freshness, so this is polled sparingly.
        time.sleep(15)

threading.Thread(target=check_bt_status_loop, daemon=True).start()


@app.route('/api/window/minimize', methods=['POST'])
def api_minimize():
    for w in webview.windows:
        w.hide()
    return jsonify({"status": "ok"})

@app.route('/api/window/quit', methods=['POST'])
def api_quit():
    kill_lhm()
    os._exit(0)
    return jsonify({"status": "ok"})

@app.route('/api/version')
def api_version():
    return jsonify({"version": CURRENT_VERSION})

@app.route('/api/browse')
def browse_file():
    global window_ref
    try:
        import webview
        if window_ref:
            result = window_ref.create_file_dialog(
                webview.OPEN_DIALOG, 
                allow_multiple=False,
                file_types=('Executables (*.exe)', 'All files (*.*)')
            )
            if result and len(result) > 0:
                return jsonify({"path": result[0]})
    except Exception as e:
        print("Error browse:", e)
    return jsonify({"path": ""})

@app.route('/api/update_check')
def api_update_check():
    return jsonify({"available": UPDATE_AVAILABLE, "version": NEW_VERSION_NAME})

@app.route('/api/force_update_check', methods=['POST'])
def api_force_update_check():
    try:
        data = fetch_latest_release()
        is_newer = apply_release_data(data)
        return jsonify({
            "available": is_newer,
            "version": data.get("tag_name", "") if is_newer else CURRENT_VERSION,
            "firmware_available": UPDATE_AVAILABLE,
            "app_available": APP_UPDATE_AVAILABLE
        })
    except Exception as e:
        return jsonify({"available": False, "error": str(e)})

@app.route('/api/app_update_check')
def api_app_update_check():
    return jsonify({"available": APP_UPDATE_AVAILABLE, "version": APP_UPDATE_VERSION})

@app.route('/api/do_app_update', methods=['POST'])
def api_do_app_update():
    if not APP_UPDATE_AVAILABLE or not APP_UPDATE_URL:
        return jsonify({"success": False, "error": "No app update available"})

    if not getattr(sys, 'frozen', False):
        return jsonify({"success": False, "error": "Self-update only works on the packaged .exe, not when running from source"})

    old_exe = sys.executable
    new_exe = old_exe + ".update"

    try:
        urllib.request.urlretrieve(APP_UPDATE_URL, new_exe)
    except Exception as e:
        return jsonify({"success": False, "error": f"Download failed: {e}"})

    # A running .exe can't overwrite itself, so a small batch script waits for
    # this process to fully exit, swaps the files, and relaunches. PyInstaller's
    # onefile bootloader runs the app inside a Job Object that kills every
    # child/detached process it spawned once the app exits, so a plain detached
    # Popen (even with DETACHED_PROCESS/CREATE_BREAKAWAY_FROM_JOB) still gets
    # killed alongside it. Task Scheduler runs the script under a completely
    # separate process tree owned by the OS, which survives that cleanup.
    pid = os.getpid()
    task_name = f"BindDeckUpdate_{pid}"
    bat_path = os.path.join(tempfile.gettempdir(), "binddeck_updater.bat")
    bat_content = (
        "@echo off\r\n"
        "setlocal\r\n"
        f"set PID={pid}\r\n"
        ":waitloop\r\n"
        "tasklist /FI \"PID eq %PID%\" | find \"%PID%\" >nul\r\n"
        "if not errorlevel 1 (\r\n"
        "    timeout /t 1 /nobreak >nul\r\n"
        "    goto waitloop\r\n"
        ")\r\n"
        "timeout /t 1 /nobreak >nul\r\n"
        f"move /Y \"{new_exe}\" \"{old_exe}\"\r\n"
        f"start \"\" \"{old_exe}\"\r\n"
        f"schtasks /Delete /TN \"{task_name}\" /F\r\n"
        "del \"%~f0\"\r\n"
    )
    try:
        with open(bat_path, 'w') as f:
            f.write(bat_content)

        subprocess.run(
            ["schtasks", "/Create", "/TN", task_name, "/TR", f'cmd /c "{bat_path}"', "/SC", "ONCE", "/ST", "23:59", "/F"],
            check=True, capture_output=True, text=True
        )
        subprocess.run(
            ["schtasks", "/Run", "/TN", task_name],
            check=True, capture_output=True, text=True
        )
    except subprocess.CalledProcessError as e:
        return jsonify({"success": False, "error": f"Could not schedule update: {e.stderr or e}"})
    except Exception as e:
        return jsonify({"success": False, "error": f"Could not schedule update: {e}"})

    def delayed_exit():
        time.sleep(0.5)
        kill_lhm()
        os._exit(0)
    threading.Thread(target=delayed_exit, daemon=True).start()

    return jsonify({"success": True})

@app.route('/api/do_update', methods=['POST'])
def api_do_update():
    global serial_port
    if not UPDATE_AVAILABLE or not NEW_VERSION_URL:
        return jsonify({"success": False, "error": "No update available"})
        
    port_to_flash = find_esp32_port()
    if not port_to_flash:
        return jsonify({"success": False, "error": "Device not connected via USB"})
        
    if serial_port and serial_port.is_open:
        try:
            serial_port.write(b"CMD:UPDATE\n")
            time.sleep(0.5)
            serial_port.close()
            serial_port = None
        except:
            pass
            
    try:
        urllib.request.urlretrieve(NEW_VERSION_URL, "firmware_update.bin")
        import esptool
        cmd = ["--port", port_to_flash, "--baud", "460800", "write_flash", "-z", "0x10000", "firmware_update.bin"]
        esptool.main(cmd)
        if os.path.exists("firmware_update.bin"):
            os.remove("firmware_update.bin")
        return jsonify({"success": True})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)})

# --- ESP32 MONITOR ---
def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = port.description.lower()
        if "ch340" in desc or "cp210" in desc or "serial" in desc or "bluetooth" in desc or "bth" in desc:
            return port.device
    return None

lhm_session = requests.Session()

def get_lhm_cpu_temp():
    try:
        response = lhm_session.get("http://127.0.0.1:8085/data.json", timeout=1)
        data = response.json()
        def find_temp(node, is_cpu=False):
            if isinstance(node, dict):
                if "cpu.png" in node.get("ImageURL", "") or "Intel" in node.get("Text", "") or "AMD" in node.get("Text", ""):
                    is_cpu = True
                if is_cpu and node.get("Text") == "Temperatures":
                    children = node.get("Children", [])
                    if children:
                        val = children[0].get("Value", "0")
                        return float(val.replace(",", ".").replace(" °C", "").strip())
                for child in node.get("Children", []):
                    result = find_temp(child, is_cpu)
                    if result is not None:
                        return result
            return None
        temp = find_temp(data)
        if temp is not None:
            return temp
    except:
        pass
    return 0.0

udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def serial_read_loop():
    global serial_port
    while True:
        try:
            sp = serial_port
            if sp and sp.is_open and sp.in_waiting > 0:
                line = sp.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    handle_device_line(line)
        except:
            pass
        time.sleep(0.02)

def udp_listen_loop():
    global device_ip
    try:
        listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listen_sock.bind(('0.0.0.0', 4211))
        while True:
            data, addr = listen_sock.recvfrom(1024)
            line = data.decode('utf-8', errors='ignore').strip()
            # Learn the device's IP so we can unicast CFG/CMD back to it
            # (the primary Wi-Fi path is a broadcast, but unicast is more
            # reliable on networks that filter directed/broadcast traffic).
            if addr:
                device_ip = addr[0]
            if line:
                handle_device_line(line)
    except Exception as e:
        print("UDP Listen Error:", e)

# GPUtil shells out to nvidia-smi.exe on every call (it spawns a fresh process,
# there is no persistent handle). Polling it once a second means launching a new
# process 60 times a minute forever, so it gets its own slower-paced thread and
# hardware_loop just reads the cached result.
gpu_usage_cached = 0.0
gpu_temp_cached = 0.0
GPU_POLL_INTERVAL = 3

def gpu_poll_loop():
    global gpu_usage_cached, gpu_temp_cached
    while True:
        try:
            gpus = GPUtil.getGPUs()
            if len(gpus) > 0:
                gpu_usage_cached = gpus[0].load * 100
                gpu_temp_cached = gpus[0].temperature
            else:
                gpu_usage_cached = 0.0
                gpu_temp_cached = 0.0
        except Exception:
            gpu_usage_cached = 0.0
            gpu_temp_cached = 0.0
        time.sleep(GPU_POLL_INTERVAL)

# The set of local IPv4 broadcast addresses barely ever changes while the app is
# running, so it is recomputed on a TTL instead of on every second-long tick.
_broadcast_ips_cache = []
_broadcast_ips_cache_time = 0
BROADCAST_CACHE_TTL = 30

def get_broadcast_ips():
    global _broadcast_ips_cache, _broadcast_ips_cache_time
    now = time.time()
    if now - _broadcast_ips_cache_time < BROADCAST_CACHE_TTL:
        return _broadcast_ips_cache

    ips = []
    for interface, snics in psutil.net_if_addrs().items():
        for snic in snics:
            if snic.family == socket.AF_INET and snic.netmask and snic.address != '127.0.0.1':
                try:
                    ip_parts = snic.address.split('.')
                    mask_parts = snic.netmask.split('.')
                    bcast_parts = [str(int(ip_parts[i]) | (255 - int(mask_parts[i]))) for i in range(4)]
                    ips.append('.'.join(bcast_parts))
                except:
                    pass

    _broadcast_ips_cache = ips
    _broadcast_ips_cache_time = now
    return ips

def hardware_loop():
    global serial_port
    while True:
        conn_type = config.get("app", {}).get("connection_type", "usb")

        if conn_type != "wifi":
            if not serial_port or not serial_port.is_open:
                port = find_esp32_port()
                if port:
                    try:
                        serial_port = serial.Serial(port, 115200, timeout=1)
                    except:
                        serial_port = None

        cpu_usage = psutil.cpu_percent(interval=None)
        cpu_temp = get_lhm_cpu_temp()
        gpu_usage = gpu_usage_cached
        gpu_temp = gpu_temp_cached

        if SIMULATE_TEMP:
            cpu_temp = 88
            gpu_temp = 88

        data_str = f"C:{int(cpu_temp)},U:{int(cpu_usage)},G:{int(gpu_temp)},V:{int(gpu_usage)}\n"

        # Zero-Config Wi-Fi Broadcast to all interfaces
        try:
            udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            # Standard 255.255.255.255
            udp_socket.sendto(data_str.encode('utf-8'), ('255.255.255.255', 4210))

            # Subnet directed broadcasts
            for bcast_ip in get_broadcast_ips():
                try:
                    udp_socket.sendto(data_str.encode('utf-8'), (bcast_ip, 4210))
                except:
                    pass
        except:
            pass

        if conn_type == "usb":
            if serial_port and serial_port.is_open:
                try:
                    serial_port.write(data_str.encode('utf-8'))
                except:
                    try:
                        serial_port.close()
                    except:
                        pass
                    serial_port = None
        
        time.sleep(1)


def start_lhm():
    lhm_path = os.path.join(base_path, 'LibreHardwareMonitor', 'LibreHardwareMonitor.exe')
    if os.path.exists(lhm_path):
        try:
            for proc in psutil.process_iter(['name']):
                if proc.info['name'] == 'LibreHardwareMonitor.exe':
                    return
            # Not running, start it
            import win32api
            import win32con
            import win32process
            # Using ShellExecute to properly request elevation if needed, but wait, Popen might fail with Access Denied if it needs elevation.
            # ShellExecute with 'runas' will trigger UAC.
            win32api.ShellExecute(0, 'runas', lhm_path, '', os.path.dirname(lhm_path), win32con.SW_HIDE)
        except Exception as e:
            print("Error launching LHM:", e)


def kill_lhm():
    try:
        import psutil
        for proc in psutil.process_iter(['name']):
            if proc.info['name'] == 'LibreHardwareMonitor.exe':
                proc.kill()
    except:
        pass

def main():
    start_lhm()
    load_config()
    
    # Arrancar monitor de hardware en background
    threading.Thread(target=hardware_loop, daemon=True).start()
    threading.Thread(target=gpu_poll_loop, daemon=True).start()
    threading.Thread(target=serial_read_loop, daemon=True).start()
    threading.Thread(target=udp_listen_loop, daemon=True).start()

    # BLE config channel: works with no WiFi and no USB cable.
    if ble_link is None:
        print(f"[ble] ble_link.py not importable ({_ble_import_error}) - "
              "is it next to pc_monitor.py? BLE config disabled.")
        if _ble_import_tb:
            print("----- ble_link import traceback -----")
            print(_ble_import_tb.rstrip())
            print("-------------------------------------")
    elif not ble_link_obj.start():
        print(f"[ble] bleak unavailable ({ble_link_obj.error}) - "
              "'pip install bleak'. BLE config disabled.")
        if ble_link_obj.error_traceback:
            print("----- bleak import traceback -----")
            print(ble_link_obj.error_traceback.rstrip())
            print("-----------------------------------")
    else:
        print("[ble] config link started")
    
    # Enganchar teclas F13-F20
    if keyboard is None:
        print("AVISO: modulo 'keyboard' nao instalado — macros F13-F20 desativadas.")
    else:
        keyboard.hook(on_key_event, suppress=False)

    import pystray


    from PIL import Image

    global window_ref
    if webview is None:
        print("ERRO: modulo 'pywebview' nao instalado — nao e possivel abrir a janela.")
        sys.exit(1)
    window = webview.create_window('BindDeck', app, width=1200, height=950, background_color='#001f3f')
    window_ref = window
    force_quit = False
    
    def show_window(icon, item):
        window.show()
    
    def quit_app(icon, item):
        nonlocal force_quit
        force_quit = True
        icon.stop()
        window.destroy()
        kill_lhm()
        os._exit(0)

    def setup_tray():
        try:
            image = Image.open(os.path.join(base_path, 'static', 'logo.png'))
            menu = pystray.Menu(
                pystray.MenuItem("Open", show_window, default=True),
                pystray.MenuItem("Quit", quit_app)
            )
            icon = pystray.Icon("BindDeck", image, "BindDeck", menu)
            icon.run()
        except Exception as e:
            print("Tray error:", e)

    threading.Thread(target=setup_tray, daemon=True).start()

    def on_closing():
        nonlocal force_quit
        if force_quit:
            return True
        
        mode = config.get("app", {}).get("closeMode", "ask")
        if mode == "quit":
            force_quit = True
            kill_lhm()
            os._exit(0)
            return True
        elif mode == "minimize":
            window.hide()
            return False
        else:
            # ask mode
            def ask_user():
                # We show a modal in the frontend
                window.evaluate_js('if(typeof showCloseModal === "function") showCloseModal(); else window.location.reload();')
            threading.Thread(target=ask_user).start()
            return False

    window.events.closing += on_closing
    webview.start()


@app.route('/api/flash', methods=['POST'])
def api_flash():
    import time

    try:
        # Simulate flash
        time.sleep(2)
        return jsonify({"success": True})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)})

@app.route('/api/upload_anim', methods=['POST'])
def upload_anim():
    if 'file' not in request.files:
        return jsonify({"success": False, "error": "No file part"})
    file = request.files['file']
    if file.filename == '':
        return jsonify({"success": False, "error": "No selected file"})
    if file:
        try:
            return jsonify({"success": True})
        except Exception as e:
            return jsonify({"success": False, "error": str(e)})

if __name__ == "__main__":
    main()
