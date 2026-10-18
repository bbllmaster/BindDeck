<div align="center">
  
  # BindDeck Companion for ESP32
  
  **An open-source, customizable programmable macro controller, powered by ESP32 and an intuitive PC companion app.**

  [![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-%23ea4aaa?style=flat&logo=github)](https://github.com/sponsors/SanX18)
  [![Buy me a coffe](https://img.shields.io/badge/Sponsor-%E2%9D%A4-%23ea4aaa?style=flat&logo=buymeacoffe)](buymeacoffee.com/sanx18)
  [![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio)](https://platformio.org/)
  [![Python](https://img.shields.io/badge/Python-3.x-blue?logo=python)](https://www.python.org/)

  ![BindDeck Preview](binddeck-preview.png)

</div>

---

If you found the project useful and want to support future designs, thank you very much for the support ❤️:

This is the **bbllmaster/BindDeck** fork. The upstream (SanX18/BindDeck) PC app is no longer used here — the companion program lives in this repository under [`pc_script/`](pc_script/).

---

## 🌟 BindDeck: Your Smart Macro Pad with ESP32 and OLED Screen

BindDeck is a complete hardware and software ecosystem designed to boost your productivity and your setup. It turns a simple ESP32 microcontroller into a powerful custom macro keyboard, equipped with an interactive OLED screen, a rotary encoder for dial control, and mechanical buttons. It is the ideal accessory for streamers, programmers, video editors, and any technology enthusiast.

🖥️ **PC companion app:** this repository's [`pc_script/`](pc_script/) folder (Windows, Python + [bleak](https://github.com/hbldh/bleak), packaged as an `.exe`).

## 🚀 What does the application do?

BindDeck is not just a shortcut keyboard; it is an interactive tool. Everything you see on your PC screen syncs instantly with your physical device:

* **Launcher and Shortcuts (8 Buttons):** Assign complex keyboard combinations (e.g. `CTRL+SHIFT+S`), launch your favorite programs (`calc.exe`, `spotify.exe`) or execute entire blocks of text with a single press.
* **Infinite Rotary Encoder (KY-040):** Turn the dial left or right with stepped tactile feedback to adjust the overall volume, zoom, navigate tabs or use undo/redo. Plus, pressing it acts as an extra button!
* **Real-Time Hardware Monitor:** Its OLED screen not only shows cute animations when you press the buttons; at rest it acts as a telemetry monitor, showing your CPU and GPU temperatures and load (usage) in real-time.
* **Customizable Texts:** Through the app, you can write what text you want to appear on the OLED screen individually for each of the buttons.
* **Hybrid Dual Connectivity (Wired or Wireless):** You can use it via USB cable, or 100% Wirelessly. It sends your macros to the PC using Bluetooth LE (for low battery consumption) and receives PC temperature data via Wi-Fi UDP Zero-Config.
* **Per-Button Key Remapping:** Each button can be assigned its own USB HID key code (function keys `F1`–`F24`, letters, arrows, editing keys, modifiers, etc.) — configured from the PC app or the hotspot web page, and saved to the device's NVS.

---

## 📺 Display Support (SSD1306 / SH1106 / SH1107)

The firmware now supports three OLED controllers, selected at **compile time** via a PlatformIO environment:

| PlatformIO environment | Chip        | Display driver |
| ---------------------- | ----------- | -------------- |
| `esp32dev`             | ESP32       | SSD1306 (0.96" I2C, 128x64) |
| `esp32dev_sh1106`      | ESP32       | SH1106         |
| `esp32dev_sh1107`      | ESP32       | SH1107         |
| `esp32c3`              | ESP32-C3    | SH1106 (default for C3) |
| `esp32c3_ssd1306`      | ESP32-C3    | SSD1306        |

Build and flash for your screen/chip with, e.g.:

```bash
pio run -e esp32dev_sh1106          # compile
pio run -e esp32dev_sh1106 -t upload   # flash over USB
```

The classic ESP32 and the ESP32-C3 use different bootloader offsets — when merging a single flashable `.bin` with `esptool merge-bin`, the C3 bootloader sits at `0x0` and the classic ESP32 bootloader at `0x1000`; the merged file is then written to `0x0` as a whole. Pre-merged binaries are published on the firmware release channel (see *Firmware Download* below).

---

## 🔧 Configuration: Hotspot Web Page + OTA + BLE CFG

BindDeck can be configured in three complementary ways.

### 1. Device Hotspot (Wi-Fi AP) Config Mode
Power on the device while **holding the rotary encoder press button**. The device raises its own Wi-Fi access point named `BindDeck-XXXX` (`XXXX` = last two bytes of the MAC). Connect to it and open **http://192.168.4.1** in a browser. The web page lets you set:

* **Button mapping** — what each of the 8 buttons does.
* **Per-button key event** — pick the USB HID key from a dropdown for each button (same source as the PC app's `CFG:KEYS:`).
* **Encoder pin** — GPIO used for the rotary encoder press.
* **Brightness**, **animation mode** (Circles / Flash / Minimal), and **auto-sleep** timeout.

When done, hold the encoder button again to exit; the device returns to Bluetooth LE mode. *(While in config mode the device deliberately keeps **BLE off**, because BLE and the softAP compete for the same 2.4 GHz radio — keeping BLE off makes the hotspot reliably connectable.)*

### 2. OTA Wireless Update
On the same config web page, open **http://192.168.4.1/update** to upload a firmware `.bin` (a full merged image or a single app image). It is written to the inactive OTA partition and the device reboots automatically. OTA is only reachable while the device is in config mode (the hotspot/AP exists only then), so it cannot be triggered over the normal BLE-only link.

### 3. BLE CFG Channel
The PC companion app can send `CFG:` commands over the bonded BLE config service to configure the device live (button keys, pins, brightness, animation, sleep, Wi-Fi, timezone). These commands are identical to the hotspot web page — what you set from one is reflected in the other.

---

## ⌨️ Per-Button Key Remapping

Each button is independently configurable. The PC app (or the hotspot web page) lets you choose the exact USB HID key each button sends — `F1`–`F24`, letters, digits, arrow keys, editing keys (Home/End/PgUp/PgDn/Ins/Del), and modifiers — then writes it to the device's NVS (`CFG:KEYS:`) so it survives reboots. No recompile is needed to change a button's action.

---

## 🖨️ Print Profile Details

This print profile is optimized and tested in **Bambu Studio** to ensure the best surface quality, strength, and ease of assembly.

### ⚙️ Recommended Settings:

* **Nozzle:** 0.4 mm
* **Layer Height:** 0.20 mm (or 0.16 mm for greater detail)
* **Wall Loops:** 2 – 3 walls
* **Infill:** 15% – 20% (Grid / Gyroid)
* **Supports:** Review according to plate orientation (tree supports recommended if applicable)*
* **Bed Adhesion:** Textured PEI Plate / Smooth PEI (Clean with soap and water if necessary)

### 🧵 Materials:

* PLA / PLA+ (Recommended for decorative or indoor use)
* PETG* (Recommended if mechanical strength or higher temperature resistance is required)

### 📦 Profile Contents:

* Organized plate with all parts ready to print directly from the **Bambu Handy** app or from the slicer.
* Optimized geometry with no need for additional adjustments.

💬 *If you liked the design, don't forget to leave your rating ⭐⭐⭐⭐⭐, photos of your result, and give it a Boost if you found it useful!*

---

## 🛠️ Hardware Used (BOM)

To assemble the electronics for this case, you will need very inexpensive and accessible components.

1. **Microcontroller:** ESP32 (the classic ESP32-WROOM-32 Dev Kit model is recommended) — also works on **ESP32-C3** (use the `esp32c3*` environments).
2. **Screen:** 0.96" I2C OLED screen (128x64). Supported controllers: **SSD1306**, **SH1106**, or **SH1107** — pick the matching PlatformIO environment above.
3. **Rotary Encoder:** 1x KY-040 Module (replaces the old potentiometer, offering infinite rotation and button/click function when pressed).
4. **Switches / Buttons:** 8x Mechanical Switches. (Outemu Red switches were used in this project. Being linear and quiet, they offer a smooth, fast feel perfect for macros without the classic noisy "click").
5. **ON/OFF Switch:** 1x SS-12F15
6. **Battery and Charging (Wireless):** 1x 3.7V LiPo Battery (1400mAh)(49×34×5.4 mm) + 1x TP4056 Charger Module (USB-C).
7. **Wiring:** Dupont cables to make the internal connections (or a custom PCB).
8. **Extras:** Keycaps of your choice to decorate the mechanical switches and a "Knob" or cap for the encoder.
9. **Battery Sensor:** 2x 10K Ohm Resistors (used to create a voltage divider and allow the ESP32 to read the remaining charge percentage without burning out).

---

## 🔌 Physical Connection Diagram

### 🔋 Power and Battery Reader (TP4056)
* **Positive (+) Battery Wire:** Connect to the B+ pin of the TP4056.
* **Negative (-) Battery Wire:** Connect to the B- pin of the TP4056.
* **OUT+ (Output) of the TP4056:** Connect to the VIN or 5V pin of the ESP32.
* **OUT- (Output) of the TP4056:** Connect to the GND pin of the ESP32.

### ⚡ Battery Percentage Sensor (Voltage Divider)
Since the ESP32 cannot read the 4.2V from the battery directly, we need to lower that voltage by half using two 10K resistors as follows:
* Solder one 10K Resistor from the OUT+ of the TP4056 module and connect it to the GPIO 35 pin of the ESP32.
* Solder the other 10K Resistor connecting the GPIO 35 pin to the GND pin of the ESP32. (This will divide the voltage in half, allowing BindDeck to show you an accurate battery icon on the screen).

### 📺 OLED Screen (I2C Communication)
* **VCC:** Connect to 3.3V of the ESP32.
* **GND:** Connect to GND of the ESP32.
* **SDA (Data Line):** Connect to GPIO 21.
* **SCL (Clock Line):** Connect to GPIO 22.

### 🎛️ Rotary Encoder (KY-040)
* **+ (VCC):** Connect to 3.3V of the ESP32.
* **GND:** Connect to GND of the ESP32.
* **CLK (Clock):** Connect to GPIO 18.
* **DT (Data):** Connect to GPIO 19.
* **SW (Encoder Button):** Connect to GPIO 5.

### ⌨️ Mechanical Switches (Outemu Red)
Each mechanical switch has two pins. One pin from all switches must be connected to GND (Ground). The remaining pin of each switch is connected to the following ESP32 pins:

* **Switch 1:** GPIO 13
* **Switch 2:** GPIO 12
* **Switch 3:** GPIO 14
* **Switch 4:** GPIO 27
* **Switch 5:** GPIO 26
* **Switch 6:** GPIO 25
* **Switch 7:** GPIO 33
* **Switch 8:** GPIO 32

*(💡 Tip: You can bridge or "daisy-chain" all the GND pins of the 8 switches, the screen, and the encoder with a single wire to run a single wire to the ESP32's GND pin).*

> **ESP32-C3 note:** the C3 reserves GPIO 11 (VDD_SPI) and 12–17 (flash I/O), plus GPIO 18/19 (USB-CDC console). These must **not** be used for buttons/peripherals. The firmware's default button pins are safe placeholders; set your real wiring with the PC app's `CFG:PINS:` or hard-code `DEFAULT_SWITCH_PINS` in `src/main.cpp`. On the C3 the encoder is polled in `loop()` (not interrupt-driven) to avoid flash-related interrupt storms.

---

## 💻 Installation and Software All in One!

Forget about compiling code, installing Python, or using complex programming environments. The entire process is simplified so you can manage it directly from the PC companion app:

1. Connect your ESP32 to the computer using a USB cable.
2. Open the **BindDeck** desktop application from the repository's [`pc_script/`](pc_script/) folder (built with Python + bleak; a packaged `.exe` is provided).
3. **Install Firmware:** The app can flash the firmware onto your ESP32 automatically. Build the matching environment first (`pio run -e <env>` — see *Display Support*), then let the app flash it.
4. **Administrator Permissions:** The first time you open the App, Windows will ask for Administrator permissions. ⚠️ It is essential to accept them so that the internal reader (LibreHardwareMonitor) can read the temperature of your processor (CPU) and your graphics card (GPU) to send them to the small OLED screen.
5. **Configure over the hotspot:** Prefer to set buttons and keys without a cable? Power on while holding the encoder button, join the `BindDeck-XXXX` Wi-Fi, and open **http://192.168.4.1** (see *Configuration* above).
6. **Ready to customize!** From the same application, you can configure what each button does, the action of turning the KY-040 encoder, change the screen texts, and adjust your entire ecosystem 100% visually.

*(The entire system works 100% locally and privately on your network, without the need for a connection to external servers or accounts).*

---

## 🔗 Multiple Devices, Primary Host and the OLED Settings Menu

Running several BindDecks at once is **supported**:

* **Unique identity per deck:** each device advertises its own BLE name `BindDeck-XXXX` (the last two bytes of its MAC), its own IRK, and its own HID serial, model and PID. Windows therefore sees every deck as a separate keyboard and keeps them all connected at the same time, so each deck can be added or removed independently.
* **Per-device events and configuration:** button events carry the device id (`BTN:<devId>:<btn>`, where `btn` is `0`–`7` for the keys and `8` for the encoder press). The PC app lists every `BindDeck-*` device, keeps each link alive and stores the configuration per `deviceId`, so each deck can have its own key map.

### Single primary host

* **HID input goes to the primary host only.** The first computer that subscribes to the HID report becomes the primary host; a second host (a phone, or another PC) can connect but never receives keystrokes or volume changes.
* **The first HID host is bound.** Its identity address is stored in the device's NVS and every other computer or phone is **rejected at connection time** — even with the bound PC switched off, a phone cannot take over. The PC app on that same machine uses the same adapter address, so configuration (`CFG:` / `BTN:`) keeps working normally.

### OLED settings menu

* **Hold the rotary encoder press for ~1.5 s** in normal mode to open the menu. A short click keeps its usual meaning (F21 + `BTN:<devId>:8`).
* Items: **`Host`** — the bound host address (read-only), **`Clear Host`** — erase the binding, **`Exit`**.
* **Rotate** to move the selection, **click** to confirm, **hold** (~1.5 s) to leave.
* **`Clear Host`** drops the live links and erases the stored bonds; the device shows *Host cleared / any host may pair* and the next host that connects and pairs becomes the new bound host.

> **Upgrade note:** this firmware generation changed the IRK and the HID serial/model/PID (WS-273/274). After upgrading, **pair each deck once again** — a one-time step. Routine app-only updates keep the pairing, as described in the user manual.

---

## 📦 Firmware Download & Flashing

Per the project's convention, all merged flashable binaries (merged image, OTA app-only image, and partition-fix images) are published on the rolling delivery channel **「BindDeck 固件发布（滚动交付通道）」** task (**WS-260**). Grab the latest `.bin` for your chip + screen there and flash it (over USB from `pc_script`, or OTA via the hotspot page). When merging a single file with `esptool merge-bin`, write the whole image from `0x0` (the C3 bootloader offset is already `0x0`; the classic ESP32 bootloader is at `0x1000`).

---

## 🇪🇸 Versión en Español / Spanish Version

<div align="center">
  
  # BindDeck Companion for ESP32
  
  **Un controlador macro programable open-source y personalizable, potenciado por ESP32 y una intuitiva App de PC.**

  [![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-%23ea4aaa?style=flat&logo=github)](https://github.com/sponsors/SanX18)
  [![Buy me a coffe](https://img.shields.io/badge/Sponsor-%E2%9D%A4-%23ea4aaa?style=flat&logo=buymeacoffe)](buymeacoffee.com/sanx18)
  [![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio)](https://platformio.org/)
  [![Python](https://img.shields.io/badge/Python-3.x-blue?logo=python)](https://www.python.org/)

  ![BindDeck Preview](binddeck-preview.png)

</div>

---

Si encontraste útil el proyecto y quieres apoyar futuros diseños, muchas gracias por el apoyo ❤️:

Este es el fork **bbllmaster/BindDeck**. Ya no se usa la App de PC del upstream (SanX18/BindDeck): el programa compañero vive en este repositorio, en la carpeta [`pc_script/`](pc_script/).

---

## 🌟 BindDeck: Tu Macro Pad Inteligente con ESP32 y Pantalla OLED

BindDeck es un ecosistema completo de hardware y software diseñado para potenciar tu productividad y tu setup. Convierte un simple microcontrolador ESP32 en un potente teclado de macros personalizado, equipado con una pantalla OLED interactiva, un encoder rotativo para control de dial y botones mecánicos. Es el accesorio ideal para streamers, programadores, editores de vídeo y cualquier entusiasta de la tecnología.

🖥️ **App de PC compañera:** la carpeta [`pc_script/`](pc_script/) de este repositorio (Windows, Python + [bleak](https://github.com/hbldh/bleak), empaquetada como `.exe`).

## 🚀 ¿Qué hace la aplicación?

BindDeck no es solo un teclado de atajos; es una herramienta interactiva. Todo lo que ves en la pantalla de tu PC se sincroniza al instante con tu dispositivo físico:

* **Lanzador y Atajos (8 Botones):** Asigna combinaciones complejas de teclado (ej. `CTRL+SHIFT+S`), lanza tus programas favoritos (`calc.exe`, `spotify.exe`) o ejecuta bloques enteros de texto con una sola pulsación.
* **Encoder Rotativo Infinito (KY-040):** Gira el dial a izquierda o derecha con tacto de pasos para ajustar el volumen general, hacer zoom, navegar por pestañas o usar deshacer/rehacer. ¡Además, al presionarlo actúa como un botón extra!
* **Monitor de Hardware en Tiempo Real:** Su pantalla OLED no solo muestra simpáticas animaciones cuando pulsas los botones; en reposo actúa como un monitor de telemetría, mostrando las temperaturas y la carga (uso) de tu CPU y GPU en tiempo real.
* **Textos Personalizables:** A través de la app, puedes escribir qué texto quieres que aparezca en la pantalla OLED de manera individual para cada uno de los botones.
* **Conectividad Dual Híbrida (Cable o Inalámbrico):** Puedes usarlo por cable USB, o de manera 100% Inalámbrica. Envía tus macros al PC usando Bluetooth LE (para un bajo consumo de batería) y recibe los datos de las temperaturas del PC vía Wi-Fi UDP Zero-Config.
* **Remapeo de teclas por botón:** Cada botón puede asignarse a su propio código de tecla USB HID (teclas de función `F1`–`F24`, letras, flechas, teclas de edición, modificadores, etc.) — configurado desde la app de PC o la página web del hotspot, y guardado en la NVS del dispositivo.

---

## 📺 Soporte de Pantalla (SSD1306 / SH1106 / SH1107)

El firmware ahora soporta tres controladores OLED, seleccionados en **tiempo de compilación** mediante un entorno de PlatformIO:

| Entorno de PlatformIO | Chip        | Controlador de pantalla |
| --------------------- | ----------- | ----------------------- |
| `esp32dev`            | ESP32       | SSD1306 (0.96" I2C, 128x64) |
| `esp32dev_sh1106`     | ESP32       | SH1106                  |
| `esp32dev_sh1107`     | ESP32       | SH1107                  |
| `esp32c3`             | ESP32-C3    | SH1106 (por defecto en C3) |
| `esp32c3_ssd1306`     | ESP32-C3    | SSD1306                 |

Compila y graba para tu pantalla/chip con, por ejemplo:

```bash
pio run -e esp32dev_sh1106            # compilar
pio run -e esp32dev_sh1106 -t upload  # grabar por USB
```

El ESP32 clásico y el ESP32-C3 usan offsets de bootloader distintos — al fusionar un único `.bin` con `esptool merge-bin`, el bootloader del C3 está en `0x0` y el del ESP32 clásico en `0x1000`; el archivo fusionado se escribe desde `0x0` como un todo. Los binarios ya fusionados se publican en el canal de firmware (ver *Descarga de Firmware* más abajo).

---

## 🔧 Configuración: Página Web Hotspot + OTA + CFG por BLE

BindDeck se puede configurar de tres formas complementarias.

### 1. Modo Configuración por Hotspot (punto de acceso Wi-Fi)
Enciende el dispositivo **manteniendo pulsado el botón del encoder rotativo**. El dispositivo levanta su propio punto de acceso Wi-Fi llamado `BindDeck-XXXX` (`XXXX` = últimos dos bytes del MAC). Conéctate a él y abre **http://192.168.4.1** en el navegador. La página web permite configurar:

* **Mapeo de botones** — qué hace cada uno de los 8 botones.
* **Evento de tecla por botón** — elige la tecla USB HID de un desplegable para cada botón (misma fuente que el `CFG:KEYS:` de la app de PC).
* **Pin del encoder** — GPIO usado para el pulsador del encoder.
* **Brillo**, **modo de animación** (Círculos / Flash / Mínimo) y **tiempo de auto-sueño**.

Cuando termines, vuelve a mantener pulsado el encoder para salir; el dispositivo vuelve al modo Bluetooth LE. *(En modo configuración el dispositivo mantiene el **BLE apagado** a propósito, porque BLE y el softAP compiten por la misma radio de 2.4 GHz — mantener BLE apagado hace que el hotspot se conecte de forma fiable).*

### 2. Actualización OTA Inalámbrica
En la misma página web de configuración, abre **http://192.168.4.1/update** para subir un firmware `.bin` (una imagen fusionada completa o solo la app). Se escribe en la partición OTA inactiva y el dispositivo se reinicia automáticamente. La OTA solo es accesible en modo configuración (el hotspot/AP solo existe entonces), por lo que no se puede disparar por el enlace BLE normal.

### 3. Canal CFG por BLE
La app de PC puede enviar comandos `CFG:` por el servicio BLE de configuración (emparejado) para configurar el dispositivo en vivo (teclas de botones, pines, brillo, animación, sueño, Wi-Fi, zona horaria). Estos comandos son idénticos a los de la página web del hotspot — lo que configures desde uno se refleja en el otro.

---

## ⌨️ Remapeo de Teclas por Botón

Cada botón es configurable de forma independiente. La app de PC (o la página web del hotspot) te deja elegir la tecla USB HID exacta que envía cada botón — `F1`–`F24`, letras, dígitos, flechas, teclas de edición (Home/End/PgUp/PgDn/Ins/Del) y modificadores — y la escribe en la NVS del dispositivo (`CFG:KEYS:`) para que sobreviva a los reinicios. No es necesario recompilar para cambiar la acción de un botón.

---

## 🖨️ Perfil de Impresión / Print Profile Details

Este perfil de impresión está optimizado y probado en **Bambu Studio** para garantizar la mejor calidad de superficie, resistencia y facilidad de montaje.

### ⚙️ Ajustes recomendados / Recommended Settings:

* **Boquilla / Nozzle:** 0.4 mm
* **Altura de capa / Layer Height:** 0.20 mm (o 0.16 mm para mayor detalle)
* **Paredes / Wall Loops:** 2 – 3 paredes
* **Relleno / Infill:** 15% – 20% (Grid / Gyroid)
* **Soportes / Supports:** Revisar según orientación del plato (árbol / tree supports recomendados si aplica)*
* **Adhesión / Bed Adhesion:** Textured PEI Plate / Smooth PEI (Limpia con agua y jabón si es necesario)

### 🧵 Filamentos sugeridos / Materials:

* PLA / PLA+ (Recomendado para uso decorativo o interior)
* PETG* (Recomendado si requiere resistencia mecánica o mayor temperatura)

### 📦 Contenido del Perfil:

* Placa organizada con todas las piezas listas para imprimir directamente desde la app **Bambu Handy** o desde el slicer.
* Geometría optimizada sin necesidad de ajustes adicionales.

💬 *Si te ha gustado el diseño, ¡no olvides dejar tu valoración ⭐⭐⭐⭐⭐, fotos de tu resultado y darle a Boost si te ha sido útil!*

---

## 🛠️ Hardware Utilizado (BOM)

Para montar la electrónica de esta carcasa, necesitarás componentes muy económicos y accesibles.

1. **Microcontrolador:** ESP32 (Se recomienda el modelo clásico ESP32-WROOM-32 Dev Kit). También funciona en **ESP32-C3** (usa los entornos `esp32c3*`).
2. **Pantalla:** Pantalla OLED de 0.96" I2C (128x64). Controladores soportados: **SSD1306**, **SH1106** o **SH1107** — elige el entorno de PlatformIO correspondiente arriba.
3. **Encoder Rotativo:** 1x Módulo KY-040 (Sustituye al antiguo potenciómetro, ofreciendo giro infinito y función de botón/clic al presionarlo).
4. **Switches / Botones:** 8x Switches Mecánicos. (En este proyecto se han utilizado switches Outemu Red. Al ser lineales y silenciosos, ofrecen un tacto suave, rápido y perfecto para macros sin el clásico "clic" ruidoso).
5. **Switch ON/OFF:** 1x SS-12F15
6. **Batería y Carga (Inalámbrico):** 1x Batería LiPo de 3.7V (1400mAh)(49×34×5.4 mm) + 1x Módulo cargador TP4056 (USB-C).
7. **Cableado:** Cables Dupont para realizar las conexiones interiores (o una PCB personalizada).
8. **Extras:** Keycaps (teclas) de tu elección para decorar los switches mecánicos y un "Knob" o tapa para el encoder.
9. **Sensor de Batería:** 2x Resistencias de 10K Ohmios (Sirven para crear un divisor de tensión y permitir que el ESP32 lea el porcentaje de carga restante sin quemarse).

---

## 🔌 Diagrama de Conexiones Físicas

### 🔋 Alimentación y Lector de Batería (TP4056)
* **Cable Positivo (+) de la Batería:** Conectar al pin B+ del TP4056.
* **Cable Negativo (-) de la Batería:** Conectar al pin B- del TP4056.
* **OUT+ (Salida) del TP4056:** Conectar al pin VIN o 5V del ESP32.
* **OUT- (Salida) del TP4056:** Conectar al pin GND del ESP32.

### ⚡ Sensor de Porcentaje de Batería (Divisor de Tensión)
Como el ESP32 no puede leer los 4.2V de la batería directamente, necesitamos bajar ese voltaje a la mitad usando dos resistencias de 10K de la siguiente manera:
* Suelta una Resistencia de 10K desde el OUT+ del módulo TP4056 y conéctala al pin GPIO 35 del ESP32.
* Suelta la otra Resistencia de 10K conectando el pin GPIO 35 hacia el pin GND del ESP32. (Esto dividirá el voltaje a la mitad, permitiendo que BindDeck te muestre un icono de batería preciso en la pantalla).

### 📺 Pantalla OLED (Comunicación I2C)
* **VCC:** Conectar a 3.3V del ESP32.
* **GND:** Conectar a GND del ESP32.
* **SDA (Línea de Datos):** Conectar al GPIO 21.
* **SCL (Línea de Reloj):** Conectar al GPIO 22.

### 🎛️ Encoder Rotativo (KY-040)
* **+ (VCC):** Conectar a 3.3V del ESP32.
* **GND:** Conectar a GND del ESP32.
* **CLK (Reloj):** Conectar al GPIO 18.
* **DT (Datos):** Conectar al GPIO 19.
* **SW (Botón del Encoder):** Conectar al GPIO 5.

### ⌨️ Switches Mecánicos (Outemu Red)
Cada switch mecánico tiene dos patillas. Una patilla de todos los switches debe ir conectada a GND (Tierra). La patilla restante de cada switch se conecta a los siguientes pines del ESP32:

* **Switch 1:** GPIO 13
* **Switch 2:** GPIO 12
* **Switch 3:** GPIO 14
* **Switch 4:** GPIO 27
* **Switch 5:** GPIO 26
* **Switch 6:** GPIO 25
* **Switch 7:** GPIO 33
* **Switch 8:** GPIO 32

*(💡 Consejo: Puedes puentear o "cadenerar" con un mismo cable todas las patillas GND de los 8 switches, la pantalla y el encoder para llevar un único cable al pin GND del ESP32).*

> **Nota ESP32-C3:** el C3 reserva los GPIO 11 (VDD_SPI) y 12–17 (I/O del flash), además de los GPIO 18/19 (consola USB-CDC). Estos **no** deben usarse para botones/periféricos. Los pines por defecto del firmware son valores seguros de marcador; define tu cableado real con el `CFG:PINS:` de la app de PC o fíjalo en `DEFAULT_SWITCH_PINS` en `src/main.cpp`. En el C3 el encoder se sondea en `loop()` (no por interrupción) para evitar tormentas de interrupciones relacionadas con el flash.

---

## 💻 Instalación y Software ¡Todo en uno!

Olvídate de compilar código, instalar Python o usar entornos de programación complejos. Todo el proceso está simplificado para que lo gestiones directamente desde la app de PC:

1. Conecta tu ESP32 al ordenador mediante un cable USB.
2. Abre la aplicación de escritorio **BindDeck** de la carpeta [`pc_script/`](pc_script/) del repositorio (hecha con Python + bleak; se proporciona un `.exe` empaquetado).
3. **Instala el Firmware:** La app puede grabar el firmware en tu ESP32 automáticamente. Compila primero el entorno adecuado (`pio run -e <env>` — ver *Soporte de Pantalla*), luego deja que la app lo grabe.
4. **Permisos de Administrador:** La primera vez que abras la App, Windows te pedirá permisos de Administrador. ⚠️ Es imprescindible aceptarlos para que el lector interno (LibreHardwareMonitor) pueda leer la temperatura de tu procesador (CPU) y tu gráfica (GPU) para mandarlas a la pequeña pantalla OLED.
5. **Configura por el hotspot:** ¿Prefieres configurar botones y teclas sin cable? Enciende manteniendo pulsado el botón del encoder, únete al Wi-Fi `BindDeck-XXXX` y abre **http://192.168.4.1** (ver *Configuración* arriba).
6. **¡Listo para personalizar!** Desde esa misma aplicación podrás configurar qué hace cada botón, la acción de girar el encoder KY-040, cambiar los textos de la pantalla y ajustar todo tu ecosistema de forma 100% visual.

*(Todo el sistema funciona de manera 100% local y privada en tu red, sin necesidad de conexión a servidores externos ni cuentas).*

---

## 🔗 Varios Dispositivos, Host Principal y Menú de Ajustes en el OLED

Usar varios BindDeck a la vez está **soportado**:

* **Identidad única por dispositivo:** cada dispositivo anuncia su propio nombre BLE `BindDeck-XXXX` (los dos últimos bytes de su MAC), su propio IRK y su propio serial, modelo y PID de HID. Windows ve cada deck como un teclado independiente y mantiene todos conectados a la vez, así que cada deck se puede añadir o quitar de forma independiente.
* **Eventos y configuración por dispositivo:** los eventos de botón llevan el id de dispositivo (`BTN:<devId>:<btn>`, donde `btn` es `0`–`7` para las teclas y `8` para el pulsador del encoder). La app de PC lista todos los dispositivos `BindDeck-*`, mantiene cada enlace y guarda la configuración por `deviceId`, de modo que cada deck puede tener su propio mapa de teclas.

### Un único host principal

* **La entrada HID va solo al host principal.** El primer ordenador que se suscribe al informe HID pasa a ser el host principal; un segundo host (un móvil u otro PC) puede conectarse, pero nunca recibe pulsaciones ni cambios de volumen.
* **El primer host HID queda vinculado.** Su dirección de identidad se guarda en la NVS del dispositivo y cualquier otro ordenador o móvil es **rechazado al conectar** — incluso con el PC vinculado apagado, un móvil no puede tomar el control. La app de PC de ese mismo equipo usa la misma dirección de adaptador, así que la configuración (`CFG:` / `BTN:`) sigue funcionando con normalidad.

### Menú de ajustes en el OLED

* **Mantén pulsado el botón del encoder ~1,5 s** en modo normal para abrir el menú. Una pulsación corta conserva su significado habitual (F21 + `BTN:<devId>:8`).
* Opciones: **`Host`** — dirección del host vinculado (solo lectura), **`Clear Host`** — borra la vinculación, **`Exit`**.
* **Gira** para mover la selección, **pulsa** para confirmar, **mantén pulsado** (~1,5 s) para salir.
* **`Clear Host`** corta los enlaces activos y borra los vínculos guardados; la pantalla muestra *Host cleared / any host may pair* y el siguiente host que se conecte y empareje pasa a ser el nuevo host vinculado.

> **Nota de actualización:** esta generación de firmware cambió el IRK y el serial/modelo/PID de HID (WS-273/274). Tras actualizar, **empareja cada dispositivo una vez más** — es un paso único. Las actualizaciones rutinarias de solo aplicación conservan el emparejamiento, como se explica en el manual de usuario.

---

## 📦 Descarga y Grabado de Firmware

Según la convención del proyecto, todos los binarios grabables fusionados (imagen fusionada, imagen OTA solo-app e imágenes de reparación de particiones) se publican en el canal de entrega continua **「BindDeck 固件发布（滚动交付通道）」** (tarea **WS-260**). Toma el último `.bin` para tu chip + pantalla y grábalo (por USB desde `pc_script`, u OTA vía la página del hotspot). Al fusionar un único archivo con `esptool merge-bin`, escribe la imagen completa desde `0x0` (el offset del bootloader del C3 ya es `0x0`; el del ESP32 clásico está en `0x1000`).

---

## 🇨🇳 简体中文版 / Simplified Chinese

<div align="center">
  
  # BindDeck 宏键盘（基于 ESP32）
  
  **一个开源、可自定义的可编程宏控制器，由 ESP32 与易用的电脑伴侣程序驱动。**

  [![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-%23ea4aaa?style=flat&logo=github)](https://github.com/sponsors/SanX18)
  [![Buy me a coffe](https://img.shields.io/badge/Sponsor-%E2%9D%A4-%23ea4aaa?style=flat&logo=buymeacoffe)](buymeacoffee.com/sanx18)
  [![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio)](https://platformio.org/)
  [![Python](https://img.shields.io/badge/Python-3.x-blue?logo=python)](https://www.python.org/)

  ![BindDeck Preview](binddeck-preview.png)

</div>

---

如果这个项目对你有用、想支持后续设计，非常感谢你的支持 ❤️：

本仓库是 **bbllmaster/BindDeck** 分支。上游（SanX18/BindDeck）的电脑端程序已不再使用——伴侣程序就放在这个仓库里，目录为 [`pc_script/`](pc_script/)。

---

## 🌟 BindDeck：带 OLED 屏、用 ESP32 打造的智能宏键盘

BindDeck 是一套完整的硬件 + 软件生态，用来提升你的效率与桌面体验。它把一块普通的 ESP32 微控制器变成一把强大的自定义宏键盘，配上一块可交互的 OLED 屏幕、一个用来旋转控制的编码器，以及机械按键。无论你是主播、程序员、视频剪辑师，还是任何科技爱好者，它都是理想配件。

🖥️ **电脑伴侣程序：** 本仓库的 [`pc_script/`](pc_script/) 目录（Windows，Python + [bleak](https://github.com/hbldh/bleak)，已打包为 `.exe`）。

## 🚀 它能做什么？

BindDeck 不只是一把快捷键键盘，它是一个可交互工具。你在电脑屏幕上看到的一切，都会与你的实体设备实时同步：

* **启动器与快捷键（8 个按键）：** 指派复杂的键盘组合（如 `CTRL+SHIFT+S`）、启动常用程序（`calc.exe`、`spotify.exe`），或一键发送整段文本。
* **无限旋转编码器（KY-040）：** 左右转动带步进触感，用来调节总音量、缩放、切换标签页，或撤销/重做。按下它还能当作一个额外的按键！
* **实时硬件监控：** OLED 屏不仅在你按键时显示可爱动画；静止时它化身为遥测监视器，实时显示 CPU 与 GPU 的温度和负载（使用率）。
* **可自定义文字：** 通过程序，你可以为每个按键单独设置想在 OLED 上显示的文字。
* **混合双连接（有线或无线）：** 可用 USB 线连接，也可 100% 无线。通过蓝牙低功耗（BLE，省电）把宏发到电脑，并通过 Wi-Fi UDP 零配置接收电脑的温度数据。
* **逐按键重映射：** 每个按键都能独立指派自己的 USB HID 键码（功能键 `F1`–`F24`、字母、方向键、编辑键、修饰键等）——既可在电脑程序里设置，也能在热点网页里设置，并保存到设备的 NVS 中。

---

## 📺 显示屏支持（SSD1306 / SH1106 / SH1107）

固件现已支持三种 OLED 控制器，**在编译时**通过 PlatformIO 环境选择：

| PlatformIO 环境         | 芯片        | 显示驱动           |
| ----------------------- | ----------- | ------------------ |
| `esp32dev`              | ESP32       | SSD1306（0.96" I2C，128x64） |
| `esp32dev_sh1106`       | ESP32       | SH1106             |
| `esp32dev_sh1107`       | ESP32       | SH1107             |
| `esp32c3`               | ESP32-C3    | SH1106（C3 默认）  |
| `esp32c3_ssd1306`       | ESP32-C3    | SSD1306            |

针对你的屏幕/芯片编译并烧录，例如：

```bash
pio run -e esp32dev_sh1106            # 编译
pio run -e esp32dev_sh1106 -t upload  # 通过 USB 烧录
```

经典 ESP32 与 ESP32-C3 的 bootloader 偏移不同——用 `esptool merge-bin` 合并成单个 `.bin` 时，C3 的 bootloader 在 `0x0`，经典 ESP32 的 bootloader 在 `0x1000`；合并文件整体从 `0x0` 写入即可。已合并好的固件发布在固件下载通道（见下文「固件下载」）。

---

## 🔧 配置方式：热点网页 + OTA + BLE CFG

BindDeck 可通过三种互补的方式配置。

### 1. 设备热点（Wi-Fi AP）配网模式
**上电时长按编码器按下键**，设备会自建一个名为 `BindDeck-XXXX` 的 Wi-Fi 热点（`XXXX` 为 MAC 地址后两字节）。连上它并打开浏览器访问 **http://192.168.4.1**。网页可设置：

* **按键映射**——8 个按键各自的功能。
* **每按键的按键事件**——为每个按键从下拉框选择 USB HID 键（与电脑端 `CFG:KEYS:` 同源）。
* **编码器脚**——旋转编码器按下键所用的 GPIO。
* **亮度**、**动画模式**（圆圈 / 闪烁 / 极简）以及**自动休眠**时间。

配置完成后再次长按编码器退出，设备回到蓝牙模式。*(配网模式下设备会主动关闭 **BLE**，因为 BLE 与 softAP 共用 2.4GHz 射频——关掉 BLE 才能让热点稳定可连。)*

### 2. OTA 无线刷机
在同一配网网页中打开 **http://192.168.4.1/update** 即可上传固件 `.bin`（整片合并镜像或单 app 镜像均可）。它会写入不活动的 OTA 分区并自动重启。OTA 只能在配网模式下访问（热点/AP 仅此时存在），所以无法在普通 BLE 链路下触发。

### 3. BLE CFG 配置通道
电脑伴侣程序可通过已配对 BLE 的配置服务发送 `CFG:` 命令，实时配置设备（按键、引脚、亮度、动画、休眠、Wi-Fi、时区）。这些命令与热点网页完全一致——任一处修改都会反映到另一处。

---

## ⌨️ 逐按键键码重映射

每个按键都可独立配置。电脑程序（或热点网页）让你为每个按键挑选确切的 USB HID 键——`F1`–`F24`、字母、数字、方向键、编辑键（Home/End/PgUp/PgDn/Ins/Del）以及修饰键——然后写入设备的 NVS（`CFG:KEYS:`），重启后依然保留。改按键动作无需重新编译。

---

## 🖨️ 3D 打印配置（Print Profile）

本打印配置在 **Bambu Studio** 中优化并测试，以保证最佳的表层质量、强度和易装配性。

### ⚙️ 推荐参数：

* **喷嘴（Nozzle）：** 0.4 mm
* **层高（Layer Height）：** 0.20 mm（追求细节可 0.16 mm）
* **墙环（Wall Loops）：** 2 – 3 圈
* **填充（Infill）：** 15% – 20%（Grid / Gyroid）
* **支撑（Supports）：** 视摆盘朝向而定（若适用建议树状支撑）*
* **底板附着（Bed Adhesion）：** 纹理 PEI 板 / 光面 PEI（必要时用肥皂水清洁）

### 🧵 材料：

* PLA / PLA+（装饰或室内使用推荐）
* PETG*（需要机械强度或更高耐温时推荐）

### 📦 配置内容：

* 整理好的摆盘，所有零件可直接从 **Bambu Handy** App 或切片软件打印。
* 优化过的几何，无需额外调整。

💬 *如果你喜欢这个设计，别忘了留下 ⭐⭐⭐⭐⭐ 评分、晒出你的成品照片，觉得有用就给它一个 Boost！*

---

## 🛠️ 硬件清单（BOM）

组装这套外壳的电路，你只需要非常便宜、易得的元件。

1. **微控制器：** ESP32（推荐经典 ESP32-WROOM-32 开发板）——同样支持 **ESP32-C3**（使用 `esp32c3*` 环境）。
2. **屏幕：** 0.96" I2C OLED 屏（128x64）。支持控制器：**SSD1306**、**SH1106** 或 **SH1107**——在上表选择对应的 PlatformIO 环境。
3. **旋转编码器：** 1x KY-040 模块（替代旧电位器，可无限旋转，按下即按钮/点击）。
4. **开关 / 按键：** 8x 机械开关。（本项目使用 Outemu 红轴；线性且安静，手感顺滑快速，适合宏而不带经典"咔哒"噪音）。
5. **电源开关：** 1x SS-12F15
6. **电池与充电（无线）：** 1x 3.7V 锂电池（1400mAh，49×34×5.4 mm）+ 1x TP4056 充电模块（USB-C）。
7. **接线：** 杜邦线做内部连接（或自制 PCB）。
8. **配件：** 自选键帽装饰机械开关，以及编码器的"旋钮"或帽。
9. **电池检测：** 2x 10K 电阻（用于分压，让 ESP32 能读取剩余电量百分比而不会烧毁）。

---

## 🔌 物理接线图

### 🔋 供电与电池读取（TP4056）
* **电池正极（+）：** 接 TP4056 的 B+。
* **电池负极（-）：** 接 TP4056 的 B-。
* **TP4056 输出 OUT+：** 接 ESP32 的 VIN 或 5V。
* **TP4056 输出 OUT-：** 接 ESP32 的 GND。

### ⚡ 电量百分比检测（分压）
ESP32 无法直接读取电池 4.2V，需用两个 10K 电阻把电压减半：
* 从 TP4056 的 OUT+ 焊一个 10K 电阻，连到 ESP32 的 GPIO 35。
* 另一个 10K 电阻把 GPIO 35 连到 ESP32 的 GND。（电压被减半，BindDeck 就能在屏上显示准确的电池图标。）

### 📺 OLED 屏（I2C 通信）
* **VCC：** 接 ESP32 的 3.3V。
* **GND：** 接 ESP32 的 GND。
* **SDA（数据线）：** 接 GPIO 21。
* **SCL（时钟线）：** 接 GPIO 22。

### 🎛️ 旋转编码器（KY-040）
* **+（VCC）：** 接 ESP32 的 3.3V。
* **GND：** 接 ESP32 的 GND。
* **CLK（时钟）：** 接 GPIO 18。
* **DT（数据）：** 接 GPIO 19。
* **SW（编码器按钮）：** 接 GPIO 5。

### ⌨️ 机械开关（Outemu 红轴）
每个机械开关有两脚。所有开关的一脚都要接到 GND（地）。每个开关剩下的那脚接到 ESP32 如下引脚：

* **开关 1：** GPIO 13
* **开关 2：** GPIO 12
* **开关 3：** GPIO 14
* **开关 4：** GPIO 27
* **开关 5：** GPIO 26
* **开关 6：** GPIO 25
* **开关 7：** GPIO 33
* **开关 8：** GPIO 32

*(💡 小贴士：你可以把 8 个开关、屏幕和编码器的所有 GND 脚用一根线"菊花链"串起来，再统一引到 ESP32 的 GND 脚。)*

> **ESP32-C3 注意：** C3 的 GPIO 11（VDD_SPI）与 12–17（Flash I/O）被内置 Flash 占用，加上 GPIO 18/19（USB-CDC 控制台），这些脚**绝不可**用作按键/外设。固件默认的按钮脚只是避开 Flash 的安全占位值；请用电脑端 `CFG:PINS:` 下发真实接线，或把 `src/main.cpp` 的 `DEFAULT_SWITCH_PINS` 写死。C3 上编码器已从中断驱动改为在 `loop()` 轮询（`readEncoder`），避免 IRAM 中调用 flash 常驻的 `digitalRead` 引发中断风暴。

---

## 💻 安装与软件，一站式搞定！

忘掉编译代码、装 Python、用复杂开发环境吧。整套流程已简化，你可以直接在电脑伴侣程序里完成：

1. 用 USB 线把 ESP32 连到电脑。
2. 打开仓库 [`pc_script/`](pc_script/) 目录里的 **BindDeck** 桌面程序（用 Python + bleak 编写，已提供打包好的 `.exe`）。
3. **安装固件：** 程序可自动把固件烧录到 ESP32。先编译对应环境（`pio run -e <env>`，见上文「显示屏支持」），再让程序烧录。
4. **管理员权限：** 首次打开程序时，Windows 会请求管理员权限。⚠️ 必须接受，内部读取器（LibreHardwareMonitor）才能读取 CPU 与 GPU 温度并发送到小 OLED 屏。
5. **通过热点配置：** 不想插线设置按键？上电时长按编码器，连上 `BindDeck-XXXX` Wi-Fi，打开 **http://192.168.4.1**（见上文「配置方式」）。
6. **开始自定义！** 在同一个程序里，你可以设置每个按键的功能、转动 KY-040 编码器的动作、修改屏幕文字，100% 可视化地调整整套生态。

*(整套系统 100% 在本地、私密的网络中运行，无需连接外部服务器或账号。)*

---

## 🔗 多设备、主主机与 OLED 设置菜单

同时运行多台 BindDeck **已受支持**：

* **每台设备身份唯一：** 每台设备广播自己的 BLE 名 `BindDeck-XXXX`（MAC 后两字节），拥有自己的 IRK、HID 序列号、型号与 PID。Windows 会把每台都识别成独立的键盘，可同时保持连接，每台都能独立添加或移除，互不影响。
* **按设备区分的按键事件与配置：** 按键事件带设备 ID（`BTN:<devId>:<btn>`，`btn` 为 `0`–`7` 表示按键、`8` 表示编码器按下）。电脑端程序会列出所有 `BindDeck-*` 设备、逐台保持连接，并按 `deviceId` 分别保存配置，因此每台可以有各自的键位映射。

### 单主机控制

* **HID 只发给主主机（primary host）。** 第一个订阅 HID 报告的电脑成为主主机；第二台主机（手机或另一台电脑）即使连上也收不到按键与音量。
* **首个 HID 主机被绑定。** 它的身份地址保存在设备 NVS 中，其他电脑/手机在连接阶段就会被**直接拒绝**——即使绑定的 PC 已关机，手机也不能接管。同一台 PC 上的电脑端程序使用相同的适配器地址，所以 `CFG:` / `BTN:` 配置照常可用。

### OLED 设置菜单

* 正常模式下**长按编码器按下键约 1.5 秒**进入菜单；短按仍保持原意（F21 + `BTN:<devId>:8`）。
* 菜单项：**`Host`** —— 已绑定主机的地址（只读）、**`Clear Host`** —— 清除绑定、**`Exit`**。
* **旋转**切换、**单击**确认、**长按**（约 1.5 秒）退出。
* **`Clear Host`** 会断开当前连接并清除已保存的配对信息，屏幕提示 *Host cleared / any host may pair*；之后重新连接并配对的主机会成为新的绑定主机。

> **升级提示：** 这一代固件改动了 IRK 以及 HID 的序列号/型号/PID（WS-273/274）。升级后**每台设备需要重新配对一次**——这是一次性的。日常只更新应用分区的升级会保留配对，详见用户手册。

---

## 📦 固件下载与烧录

按项目约定，所有合并好的可烧录固件（合并整片镜像、OTA 单 app 镜像、分区修复镜像）统一发布在滚动交付通道 **「BindDeck 固件发布（滚动交付通道）」** 任务（**WS-260**）。在那里取对应你芯片 + 屏幕的最新 `.bin`，用 `pc_script` 通过 USB 烧录，或经热点网页 OTA 升级。用 `esptool merge-bin` 合并单文件时，整体从 `0x0` 写入（C3 的 bootloader 偏移本就是 `0x0`；经典 ESP32 的 bootloader 在 `0x1000`）。
