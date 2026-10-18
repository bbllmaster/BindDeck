# -*- mode: python ; coding: utf-8 -*-
#
# Single source of truth for the Windows build. build.bat runs
# `python -m PyInstaller --clean BindDeck.spec`, so the CLI flags and this file
# can never drift apart again.

from PyInstaller.utils.hooks import collect_all

# Assets the app reads at runtime from base_path (sys._MEIPASS in one-file mode).
datas = [
    ('templates', 'templates'),
    ('static', 'static'),
    ('LibreHardwareMonitor', 'LibreHardwareMonitor'),
    ('firmware.bin', '.'),            # "Install Official Firmware"
    ('SoundVolumeView.exe', '.'),     # audio device switching
]

# These packages load submodules / binaries / data dynamically and are easily
# missed by static analysis. collect_all makes the frozen exe match a source
# run: esptool (flashing), bleak + winrt (BLE config channel), webview (the
# window), comtypes + pycaw (volume).
binaries = []
hiddenimports = ['ble_link']
for _pkg in ('esptool', 'bleak', 'winrt', 'webview', 'comtypes', 'pycaw'):
    try:
        _d, _b, _h = collect_all(_pkg)
        datas += _d
        binaries += _b
        hiddenimports += _h
    except Exception:
        # Package not installed on this build machine; skip it.
        pass


a = Analysis(
    ['pc_monitor.py'],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='BindDeck',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['static/logo.ico'],
)
