# User Manual: BindDeck (ESP32 / ESP32-C3)

Welcome to the **BindDeck** user manual, your custom macro keyboard powered by an ESP32. This manual details all the features of your device and how to get the most out of it using the desktop application.

---

## 1. Main Device Features
Your BindDeck device is not just a macro keyboard, it is an interactive desktop assistant.
* **8 Mechanical Keys (SW1 - SW8):** Fully customizable to open programs, send shortcuts, type text, or control media.
* **Smart Potentiometer:** Multifunction wheel with dynamic auto-calibration to control system volume, zoom, tab switching, undo/redo, or the volume of one specific app.
* **Integrated OLED Screen:** Displays custom animations when each key is pressed, PC performance statistics (CPU and GPU), a clock, and a device info screen. The **menu button** cycles through four screens.
* **Three ways to talk to the PC:**
  * **Bluetooth (BLE)** — acts as a wireless keyboard, **and** carries the configuration channel, so settings work with no cable and no Wi-Fi.
  * **USB** — on the classic ESP32 it carries settings and system statistics over the serial port.
  * **Wi-Fi** — optional backup channel for configuration and telemetry.

---

## 2. Choosing the Right Firmware (Important!)

There are two independent things to get right: the **chip** and the **display driver**. Flashing the wrong display driver is the most common mistake — the screen stays blank or looks garbled.

| Board | PlatformIO environment |
|---|---|
| ESP32 + SSD1306 (0.96" 128x64) | `esp32dev` |
| ESP32 + SH1106 (1.3" 128x64) | `esp32dev_sh1106` |
| ESP32 + SH1107 (1.5" 128x128) | `esp32dev_sh1107` |
| ESP32-C3 + SSD1306 (0.96" 128x64) | `esp32c3_ssd1306` |
| ESP32-C3 + SH1106 (1.3" 128x64) | `esp32c3` |

**Not sure what is on the board?** Press the **menu button** until you reach the **device info screen**. The firmware knows exactly what it is and shows it:

```
BindDeck
Chip: C3
Disp: SSD1306
FW  : v1.0.0
<build date>
```

The desktop application can read the same information (`GET /api/fw_version`), and answers the `CMD:VERSION` command with `VERSION:<version>,<display>,<chip>,<build>`.

---

## 3. Initial Setup (First Steps)

1. **Firmware Installation:** Flash the firmware that matches your board (see section 2) — via PlatformIO or by installing the included `firmware.bin`.
2. **USB Connection:** Connect the BindDeck to your PC's USB port. This powers the device and, on the classic ESP32, establishes the serial connection used for telemetry.
3. **Bluetooth Synchronization:** Pair the device in your computer's Bluetooth settings. It appears as a Bluetooth keyboard called **BindDeck**. The same pairing also opens the configuration channel.
4. **Open BindDeck App:** Start the executable on your computer. If the connection indicator (top right) is green, you are ready!

---

## 4. How the Device Talks to the PC

### Bluetooth (recommended)
Pairing gives you **two things over one connection**: the wireless keyboard, and the configuration channel used to send settings. Nothing else is needed — no cable, no Wi-Fi.

> **ESP32-C3 only.** The configuration-over-Bluetooth channel is currently implemented on the ESP32-C3. On the **classic ESP32** Bluetooth is the keyboard only and settings go over the USB serial connection.

### USB
On the **classic ESP32**, settings and telemetry travel over the serial port.

### Wi-Fi (optional backup)
There are two ways to get the device onto your network:

* **From the device itself (provisioning):** hold the **encoder button for ~2 seconds while powering on**. The device creates an access point named **`BindDeck-XXXX`**. Connect your phone to it, open **`http://192.168.4.1`**, pick your network from the list and enter the password. The credentials are stored in the device and reused on every boot.
* **From the application:** open the **WiFi** panel and enter the SSID and password, then press *Save and send to device*.

You can send configuration over any of the three channels; whichever is available will be used.

---

## 5. Using the Desktop Application

The application has a virtual interface that mimics your physical hardware. Any change you make must be pushed to the device: click the **Sync to Device** button — the **circular-arrow icon in the title bar, top right**. It sends the *whole* configuration, so you can always re-send a setting that did not arrive the first time.

### Customize Keys (Macros)
Click on any key (SW1 to SW8) in the virtual application to edit its behavior. A menu will open where you can configure:
* **Action Type:**
  * `Program`: Select the path of a `.exe` executable to open it instantly.
  * `Multimedia`: Controls like Play/Pause, Next, Previous, Mute.
  * `Keyboard shortcut`: Combinations like `Ctrl + C`, `Alt + Tab`, etc.
  * `Text`: Type an entire paragraph by pressing a single button.
* **Label (On-Screen Text):** The short name that will appear on the OLED screen when the button is pressed.
* **OLED Animation:** Choose a specific animation (Check, Lightning, Mute, Heart, etc.) to play on the physical and virtual screen simultaneously when the key is pressed.

### Configure the Potentiometer (Wheel)
The analog knob is your best ally. On the right side panel you can select its operating mode (`encMode`):
* `System Volume`: Raises and lowers the master volume.
* `Zoom`: Zooms in/out in browsers and editors (Ctrl +/-).
* `Browser Tabs`: Navigates between open tabs in your browser.
* `Undo / Redo`: Ideal for design and editing.
* `Specific App Volume`: Controls only one application — type its process name (for example `Spotify.exe`) in the field that appears.

*Technical Note:* The wheel incorporates continuous dynamic tracking logic. Every time you turn on the device, it adjusts millimeter by millimeter to the actual travel of your hardware to avoid dead zones. If you notice it lacks travel, turn it all the way to the stops once after turning it on.

### Brightness, Screen and Sleep
* Adjust the **OLED Brightness** slider to change the intensity of the screen (ideal for working at night).
* **Sleep Mode** and the **Sleep Timeout** are configured in **Settings** (gear icon, top right). When the device is idle for that long it turns the OLED off and enters deep sleep to protect the panel — press any key to wake it.

### Button GPIOs (advanced — inside Settings)
Assigning a different GPIO to each physical key is an advanced setting, so it lives in **Settings → Button GPIOs**. There you can reassign a pin per key, **Reset to Default**, or **Save** the new assignment. Reserved pins (used by the encoder, the menu button and the OLED) cannot be assigned.

---

## 6. The OLED Screens

Press the **menu button** to cycle through the four screens:

1. **PC statistics** — CPU / GPU temperature and load.
2. **Clock** — synchronised automatically over Wi-Fi (NTP). Note: the timezone is currently fixed to Central European Time, so the clock only shows the right local time in that zone.
3. **Eyes** — the interactive idle animation.
4. **Device info** — chip, display driver, firmware version and build date.

---

## 7. Telemetry and Resource Monitor (PC Monitor)
As long as the desktop application is open or minimised in the System Tray, it reads your computer's status in the background using *LibreHardwareMonitor*.
On your BindDeck's statistics screen you will be able to see in real time:
* **CPU and GPU Temperature** (in ºC).
* **CPU and GPU Load/Usage** (in %).

If any of the temperatures exceeds 85ºC, the device screen will show a warning alert to protect your equipment.

---

## 8. Updating the Firmware

There are two kinds of flash, and picking the wrong one is why pairing sometimes has to be redone:

| Situation | What to flash | Bluetooth pairing |
|---|---|---|
| **First time, or after a factory reset** | the **full** image at address `0x0` | must be paired again |
| **Routine firmware update** | the **application only** image at address `0x10000` | **kept** |

The Bluetooth pairing keys are stored in the device's `nvs` partition (`0x9000`). A full flash from `0x0` overwrites that partition, so the device forgets the keys while your PC keeps its half — the link then looks connected but does not work until you remove the device in the OS and pair it again. Flashing only the application part leaves `nvs` untouched, so routine updates never require re-pairing.

Do **not** erase/format the flash for a routine update.

---

## 9. Frequently Asked Questions / Troubleshooting

* **The indicator is red and says "Disconnected":** Make sure the USB cable supports data transmission and not just charging. Close other programs that might be occupying the COM port (such as the Arduino Serial Monitor or VSCode).
* **I press a button and it does nothing in Windows:** Verify that the device is properly paired via Bluetooth. On the classic ESP32 the USB cable carries the settings, but the actual keystrokes are always sent over Bluetooth (it acts as a wireless keyboard).
* **I changed a setting, pressed Sync, and the device did not react:** Use the **Sync to Device** button in the title bar (it re-sends everything). If it still does nothing, check the application console — every configuration command is logged as `[cfg] CFG:... -> ble` (or `udp` / `serial`). If that line is missing or shows `NOTHING`, none of the channels is connected.
* **Bluetooth shows "connected" but nothing arrives:** configuration over Bluetooth needs firmware that includes the configuration channel (v15 or newer). Check the device info screen for the firmware version. If the very first write fails with `Insufficient Authentication`, the device is not bonded yet — pair it once.
* **The screen stays blank or looks garbled:** you flashed a firmware built for a different display driver. Cycle to the device info screen to confirm what that build expects, then flash the matching one from section 2.
* **The volume wheel acts strangely on startup:** Simply turn the wheel once from one end to the other. The chip will learn its physical limits instantly and will be 100% accurate again.
* **The program doesn't read temperatures:** Make sure to run the BindDeck application as **Administrator**, as Windows requires elevated permissions for *LibreHardwareMonitor* to read motherboard and graphics card sensors.

---

Enjoy your BindDeck! If you find the project useful, you can support the creator by [buying him a coffee through GitHub Sponsors](https://github.com/sponsors/SanX18).

---
---

## 🇪🇸 Versión en Español / Spanish Version

# Manual de Usuario: BindDeck (ESP32 / ESP32-C3)

Bienvenido al manual de usuario de **BindDeck**, tu teclado macro personalizado potenciado por un ESP32. Este manual detalla todas las características de tu dispositivo y cómo sacarle el máximo partido utilizando la aplicación de escritorio.

---

## 1. Características Principales del Dispositivo
Tu dispositivo BindDeck no es solo un teclado de macros, es un asistente de escritorio interactivo.
* **8 Teclas Mecánicas (SW1 - SW8):** Totalmente personalizables para abrir programas, enviar atajos, escribir textos o controlar multimedia.
* **Potenciómetro Inteligente:** Rueda multifunción con auto-calibración dinámica para controlar el volumen del sistema, zoom, cambio de pestañas, deshacer/rehacer, o el volumen de una aplicación concreta.
* **Pantalla OLED Integrada:** Muestra animaciones personalizadas al pulsar cada tecla, estadísticas de rendimiento de tu PC (CPU y GPU), un reloj y una pantalla de información del dispositivo. El **botón de menú** recorre las cuatro pantallas.
* **Tres formas de comunicarse con el PC:**
  * **Bluetooth (BLE)** — funciona como teclado inalámbrico **y** transporta el canal de configuración, así que los ajustes funcionan sin cable y sin Wi-Fi.
  * **USB** — en el ESP32 clásico transporta los ajustes y las estadísticas del sistema por el puerto serie.
  * **Wi-Fi** — canal opcional de respaldo para configuración y telemetría.

---

## 2. Elegir el Firmware Correcto (¡Importante!)

Hay dos cosas independientes que hay que acertar: el **chip** y el **controlador de pantalla**. Instalar el controlador equivocado es el error más común — la pantalla se queda en negro o se ve distorsionada.

| Placa | Entorno de PlatformIO |
|---|---|
| ESP32 + SSD1306 (0.96" 128x64) | `esp32dev` |
| ESP32 + SH1106 (1.3" 128x64) | `esp32dev_sh1106` |
| ESP32 + SH1107 (1.5" 128x128) | `esp32dev_sh1107` |
| ESP32-C3 + SSD1306 (0.96" 128x64) | `esp32c3_ssd1306` |
| ESP32-C3 + SH1106 (1.3" 128x64) | `esp32c3` |

**¿No sabes qué lleva tu placa?** Pulsa el **botón de menú** hasta la **pantalla de información**. El firmware sabe exactamente lo que es y lo muestra:

```
BindDeck
Chip: C3
Disp: SSD1306
FW  : v1.0.0
<fecha de compilación>
```

La aplicación de escritorio puede leer la misma información (`GET /api/fw_version`), y responde al comando `CMD:VERSION` con `VERSION:<versión>,<pantalla>,<chip>,<compilación>`.

---

## 3. Configuración Inicial (Primeros Pasos)

1. **Instalación del Firmware:** Instala el firmware que corresponda a tu placa (ver sección 2) — mediante PlatformIO o instalando el `firmware.bin` incluido.
2. **Conexión USB:** Conecta el BindDeck al puerto USB de tu PC. Esto alimenta el dispositivo y, en el ESP32 clásico, establece la conexión serie usada para la telemetría.
3. **Sincronización Bluetooth:** Empareja el dispositivo en la configuración de Bluetooth de tu ordenador. Aparecerá como un teclado Bluetooth llamado **BindDeck**. Ese mismo emparejamiento abre también el canal de configuración.
4. **Abrir BindDeck App:** Inicia el ejecutable en tu ordenador. Si el indicador de conexión (arriba a la derecha) está en verde, ¡estás listo!

---

## 4. Cómo se Comunica el Dispositivo con el PC

### Bluetooth (recomendado)
El emparejamiento te da **dos cosas en una sola conexión**: el teclado inalámbrico y el canal de configuración por el que se envían los ajustes. No hace falta nada más — ni cable ni Wi-Fi.

> **Solo ESP32-C3.** El canal de configuración por Bluetooth está implementado actualmente en el ESP32-C3. En el **ESP32 clásico** el Bluetooth es solo el teclado y los ajustes van por la conexión serie USB.

### USB
En el **ESP32 clásico**, los ajustes y la telemetría viajan por el puerto serie.

### Wi-Fi (respaldo opcional)
Hay dos maneras de meter el dispositivo en tu red:

* **Desde el propio dispositivo (aprovisionamiento):** mantén pulsado el **botón del encoder unos 2 segundos mientras enciendes**. El dispositivo crea un punto de acceso llamado **`BindDeck-XXXX`**. Conecta el móvil a esa red, abre **`http://192.168.4.1`**, elige tu red de la lista e introduce la contraseña. Las credenciales se guardan en el dispositivo y se reutilizan en cada arranque.
* **Desde la aplicación:** abre el panel **WiFi**, introduce SSID y contraseña y pulsa *Guardar y enviar al dispositivo*.

Puedes enviar la configuración por cualquiera de los tres canales; se usará el que esté disponible.

---

## 5. Uso de la Aplicación de Escritorio

La aplicación tiene una interfaz virtual que imita a tu hardware físico. Cualquier cambio que hagas debe enviarse al dispositivo: pulsa el botón de **Sincronizar Dispositivo** — el **icono de flechas circulares en la barra de título, arriba a la derecha**. Envía la configuración *completa*, así que siempre puedes reenviar un ajuste que no haya llegado la primera vez.

### Personalizar las Teclas (Macros)
Haz clic en cualquier tecla (SW1 a SW8) en la aplicación virtual para editar su comportamiento. Se abrirá un menú donde podrás configurar:
* **Tipo de Acción:**
  * `Programa`: Selecciona la ruta de un ejecutable `.exe` para abrirlo al instante.
  * `Multimedia`: Controles como Play/Pausa, Siguiente, Anterior, Mutear.
  * `Atajo de teclado`: Combinaciones como `Ctrl + C`, `Alt + Tab`, etc.
  * `Texto`: Escribe un párrafo entero pulsando un solo botón.
* **Etiqueta (Texto en Pantalla):** El nombre corto que aparecerá en la pantalla OLED al pulsar el botón.
* **Animación OLED:** Elige una animación específica (Check, Rayo, Mute, Corazón, etc.) para que se reproduzca en la pantalla física y virtual simultáneamente al pulsar la tecla.

### Configurar el Potenciómetro (Rueda)
El mando analógico es tu mejor aliado. En el panel lateral derecho puedes seleccionar su modo de funcionamiento (`encMode`):
* `Volumen de Windows`: Sube y baja el volumen maestro.
* `Zoom`: Hace zoom in/out en navegadores y editores (Ctrl +/-).
* `Pestañas`: Navega entre las pestañas abiertas de tu navegador.
* `Deshacer / Rehacer`: Ideal para diseño y edición.
* `Volumen de una app concreta`: Controla solo una aplicación — escribe su nombre de proceso (por ejemplo `Spotify.exe`) en el campo que aparece.

*Nota Técnica:* La rueda incorpora una lógica de rastreo dinámico continuo. Cada vez que enciendes el dispositivo, se ajusta milimétricamente al recorrido real de tu hardware para evitar zonas muertas. Si notas que le falta recorrido, gírala hasta los topes una vez tras encenderlo.

### Brillo, Pantalla y Reposo
* Ajusta el control deslizante de **Brillo OLED** para cambiar la intensidad de la pantalla (ideal para trabajar de noche).
* El **Modo Reposo** y el **Tiempo de Reposo** se configuran en **Ajustes** (icono de engranaje, arriba a la derecha). Cuando el dispositivo queda inactivo ese tiempo, apaga el OLED y entra en sueño profundo para proteger el panel — pulsa cualquier tecla para despertarlo.

### GPIOs de los Botones (avanzado — dentro de Ajustes)
Asignar un GPIO distinto a cada tecla física es un ajuste avanzado, así que vive en **Ajustes → GPIOs de Botones**. Ahí puedes reasignar el pin de cada tecla, **Restablecer valores** o **Guardar** la nueva asignación. Los pines reservados (usados por el encoder, el botón de menú y el OLED) no se pueden asignar.

---

## 6. Las Pantallas del OLED

Pulsa el **botón de menú** para recorrer las cuatro pantallas:

1. **Estadísticas del PC** — temperatura y carga de CPU / GPU.
2. **Reloj** — se sincroniza automáticamente por Wi-Fi (NTP). Nota: la zona horaria está fijada actualmente a Europa Central, así que el reloj solo muestra la hora local correcta en esa zona.
3. **Ojos** — la animación interactiva de reposo.
4. **Información del dispositivo** — chip, controlador de pantalla, versión de firmware y fecha de compilación.

---

## 7. Telemetría y Monitor de Recursos (PC Monitor)
Mientras la aplicación de escritorio esté abierta o minimizada en la bandeja del sistema (System Tray), estará leyendo en segundo plano el estado de tu ordenador utilizando *LibreHardwareMonitor*.
En la pantalla de estadísticas de tu BindDeck podrás ver en tiempo real:
* **Temperatura de la CPU y GPU** (en ºC).
* **Carga/Uso de la CPU y GPU** (en %).

Si alguna de las temperaturas supera los 85ºC, la pantalla del dispositivo te mostrará una alerta de advertencia para proteger tu equipo.

---

## 8. Actualizar el Firmware

Hay dos tipos de grabación, y elegir la equivocada es la razón por la que a veces hay que volver a emparejar:

| Situación | Qué grabar | Emparejamiento Bluetooth |
|---|---|---|
| **Primera vez, o tras un reset de fábrica** | la imagen **completa** en la dirección `0x0` | hay que emparejar de nuevo |
| **Actualización rutinaria** | solo la **aplicación**, en la dirección `0x10000` | **se conserva** |

Las claves de emparejamiento Bluetooth se guardan en la partición `nvs` del dispositivo (`0x9000`). Una grabación completa desde `0x0` sobrescribe esa partición, así que el dispositivo olvida las claves mientras tu PC conserva su mitad — la conexión parece establecida pero no funciona hasta que eliminas el dispositivo en el sistema operativo y lo emparejas otra vez. Grabar solo la parte de aplicación no toca `nvs`, así que las actualizaciones rutinarias nunca requieren volver a emparejar.

**No** borres/formatees la flash para una actualización rutinaria.

---

## 9. Solución de Problemas Frecuentes

* **El indicador está rojo y pone "Desconectado":** Asegúrate de que el cable USB soporta transmisión de datos y no solo carga. Cierra otros programas que puedan estar ocupando el puerto COM (como el monitor serie de Arduino o VSCode).
* **Pulso un botón y no hace nada en Windows:** Comprueba que el dispositivo esté correctamente emparejado por Bluetooth. En el ESP32 clásico el cable USB envía los ajustes, pero las teclas reales siempre se envían por Bluetooth (actúa como un teclado inalámbrico).
* **Cambié un ajuste, pulsé Sincronizar y el dispositivo no reaccionó:** usa el botón **Sincronizar Dispositivo** de la barra de título (reenvía todo). Si aun así no hace nada, mira la consola de la aplicación — cada comando de configuración se registra como `[cfg] CFG:... -> ble` (o `udp` / `serial`). Si esa línea no aparece, o pone `NOTHING`, es que ningún canal está conectado.
* **El Bluetooth pone "conectado" pero no llega nada:** la configuración por Bluetooth necesita un firmware que incluya el canal de configuración (v15 o posterior). Mira la versión en la pantalla de información del dispositivo. Si la primera escritura falla con `Insufficient Authentication`, el dispositivo aún no está vinculado — empareja una vez.
* **La pantalla se queda en negro o se ve distorsionada:** has instalado un firmware compilado para otro controlador de pantalla. Ve a la pantalla de información del dispositivo para confirmar qué espera esa compilación y graba la que corresponda según la sección 2.
* **La rueda del volumen va extraña al arrancar:** Simplemente gira la rueda una vez de un extremo al otro. El chip aprenderá sus límites físicos instantáneamente y volverá a ser 100% preciso.
* **El programa no lee las temperaturas:** Asegúrate de ejecutar la aplicación BindDeck como **Administrador**, ya que Windows requiere permisos elevados para que *LibreHardwareMonitor* lea los sensores de la placa base y la gráfica.

---

¡Disfruta de tu BindDeck! Si el proyecto te resulta útil, puedes apoyar al creador [invitándole a un café a través de GitHub Sponsors](https://github.com/sponsors/SanX18).

---
---

## 🇨🇳 中文版 / Chinese Version

# BindDeck 用户手册（ESP32 / ESP32-C3）

欢迎使用 **BindDeck** —— 一台基于 ESP32 的自定义宏键盘。本手册介绍设备的全部功能，以及如何配合桌面端程序把它用得更好。

---

## 1. 设备主要特性

BindDeck 不只是宏键盘，它更像一个桌面小助手。

* **8 个机械按键（SW1–SW8）**：可自定义为打开程序、发送快捷键、输入文本或控制多媒体。
* **智能旋钮**：多功能滚轮，带动态自动校准，可控制系统音量、缩放、切换标签页、撤销/重做，或调节**某一个指定程序的音量**。
* **OLED 显示屏**：按键时播放自定义动画，显示电脑的 CPU/GPU 状态、时钟，以及设备信息。**菜单键可在 4 个界面之间循环**。
* **三种与电脑通信的方式**：
  * **蓝牙（BLE）** —— 作为无线键盘使用。（**ESP32-C3 上，同一条蓝牙连接还承载配置通道**，不插线、不连 WiFi 也能下发设置；经典 ESP32 的配置走 USB 串口，见下文。）
  * **USB** —— 经典 ESP32 通过串口传输配置和系统状态。
  * **WiFi** —— 可选的备用通道，用于配置和状态回传。

---

## 2. 选对固件（重要！）

有两件独立的事要选对：**芯片**和**屏幕驱动**。烧错屏幕驱动是最常见的错误 —— 屏幕会黑屏或显示错乱。

| 你的板子 | PlatformIO 环境名 |
|---|---|
| ESP32 + SSD1306（0.96" 128x64） | `esp32dev` |
| ESP32 + SH1106（1.3" 128x64） | `esp32dev_sh1106` |
| ESP32 + SH1107（1.5" 128x128） | `esp32dev_sh1107` |
| ESP32-C3 + SSD1306（0.96" 128x64） | `esp32c3_ssd1306` |
| ESP32-C3 + SH1106（1.3" 128x64） | `esp32c3` |

**不确定板子上是哪块屏？** 按**菜单键**切到**设备信息屏** —— 固件知道自己是什么：

```
BindDeck
Chip: C3
Disp: SSD1306
FW  : v1.0.0
Sep 30 2026 03:23:00
```

桌面端程序也能读到同样的信息（`GET /api/fw_version`），并且会响应 `CMD:VERSION` 命令，返回 `VERSION:<版本>,<屏幕>,<芯片>,<编译时间>`。

---

## 3. 初次设置

1. **烧固件**：按第 2 节选对应你板子的固件 —— 用 PlatformIO 编译，或直接烧 `firmware.bin`。
2. **连接 USB**：把 BindDeck 插到电脑 USB 口。既供电，也在经典 ESP32 上建立串口连接（用于状态回传）。
3. **蓝牙配对**：在电脑的蓝牙设置里配对，设备会以键盘形式出现，名字是 **BindDeck**。（在 ESP32-C3 上，这次配对同时打开配置通道。）
4. **打开上位机程序**：如果右上角连接指示灯是绿色，就可以开始用了。

---

## 4. 设备怎么和电脑通信

### 蓝牙（推荐）
配对一次同时给你**两样东西**：无线键盘，以及下发设置用的配置通道。
**注意：配置通道（配置走蓝牙）目前只有 ESP32-C3 支持。经典 ESP32 上蓝牙只做键盘，配置走 USB 串口。**

### USB
**经典 ESP32** 上，配置和状态都走串口。

### WiFi（可选备份）
有两种方式把设备接入你的网络：

* **在设备上配网**：**开机时按住旋钮约 2 秒**，设备会开一个叫 **`BindDeck-XXXX`** 的热点。用手机连上，打开 **`http://192.168.4.1`**，从列表里选你家网络并输入密码。凭据会保存在设备里，之后每次开机自动使用。
* **在上位机里配置**：展开 **WiFi** 面板，填 SSID 和密码，点 *保存并发送到设备*。

三条通道任一条可用即可，程序会自动用它。

---

## 5. 使用桌面端程序

程序里有一个模拟你硬件的虚拟界面。**任何改动都要下发给设备**：点标题栏右上角的 **同步到设备** 按钮（圆形箭头图标）。它会**把整套配置全量下发**，所以某次没送达的设置随时可以再同步一次。

### 自定义按键（宏）
在虚拟界面上点击任意按键（SW1–SW8）编辑它的行为，可以配置：
* **动作类型**：
  * `程序`：选一个 `.exe` 路径，按下即刻打开。
  * `多媒体`：播放/暂停、下一首、上一首、静音。
  * `键盘快捷键`：如 `Ctrl + C`、`Alt + Tab`。
  * `文本`：按一个键输入一整段文字。
* **显示文字**：按下时出现在 OLED 上的短名字。
* **OLED 动画**：选择按下时在屏幕（和虚拟界面）上播放的动画（对勾、闪电、静音、爱心等）。

### 配置旋钮
在右侧面板可以选择它的工作模式（`encMode`）：
* `系统音量`：调节主音量。
* `缩放`：在浏览器/编辑器里 Ctrl +/- 缩放。
* `浏览器标签页`：在打开的标签页之间切换。
* `撤销 / 重做`：适合设计和编辑。
* `指定程序音量`：只调节某一个程序 —— 在出现的输入框里填它的进程名（例如 `Spotify.exe`）。

*技术说明*：旋钮带连续动态跟踪逻辑。每次开机都会按你硬件的实际行程重新贴合，避免死区。如果感觉行程不够，开机后把它拧到两端极限一次即可。

### 亮度、屏幕与休眠
* 拖动 **OLED 亮度** 滑块调节屏幕强度（夜里用很合适）。
* **休眠模式**和**休眠时间**在 **设置**（右上角齿轮图标）里配置。设备闲置超过该时间后，会关闭 OLED 并进入深度睡眠以保护面板 —— **按任意键唤醒**。

### 按键 GPIO（高级 —— 在设置里）
给每个物理按键分配不同的 GPIO 属于高级设置，所以它放在 **设置 → 按键 GPIO**。在那里可以逐个改引脚、**恢复默认**、或**保存**。保留引脚（旋钮、菜单键、OLED 占用的）不能分配。

---

## 6. OLED 都有哪些界面

按**菜单键**循环 4 个界面：

1. **电脑状态** —— CPU / GPU 温度与占用。
2. **时钟** —— 通过 WiFi 走 NTP 自动同步（注意：时区目前固定为中欧时间，其他地区显示会不准）。
3. **眼睛** —— 交互式待机动画。
4. **设备信息** —— 芯片、屏幕驱动、固件版本、编译时间。

---

## 7. 状态回传与资源监视

只要上位机程序处于打开或最小化到托盘的状态，它就会在后台用 *LibreHardwareMonitor* 读取你电脑的状态。
在 BindDeck 的状态界面上可以实时看到：
* **CPU / GPU 温度**（℃）
* **CPU / GPU 占用率**（%）

任一温度超过 85℃ 时，设备屏幕会给出警告，保护你的硬件。

---

## 8. 更新固件

有两种烧写方式，选错了就是"为什么每次都要重新配对"的原因：

| 场景 | 烧什么 | 蓝牙配对 |
|---|---|---|
| **首次，或想彻底重置** | 起始地址 `0x0` 的**整片**镜像 | **需要重新配对** |
| **日常升级** | 只烧**应用部分**到 `0x10000` | **保留** |

蓝牙配对密钥存在设备的 `nvs` 分区（`0x9000`）。从 `0x0` 整片烧会**覆盖该分区**，设备忘记密钥、而电脑还留着自己那一半 —— 于是链路看着是"已连接"但用不了，必须先在系统里删除设备再重新配对。只烧应用部分不动 `nvs`，所以**日常升级永远不需要重新配对**。

> 日常升级**不要**格式化/擦除整片 flash。

---

## 9. 常见问题与排障

* **指示灯红色、显示"未连接"**：确认 USB 线支持数据传输（不只是充电线）。关掉可能占用串口的程序（Arduino 串口监视器、VSCode 等）。
* **按了键 Windows 没反应**：确认设备已正确蓝牙配对。经典 ESP32 上 USB 只负责传配置，**按键永远走蓝牙**（它是无线键盘）。
* **改了设置、点了同步，设备没反应**：用**标题栏的同步按钮**（全量下发）。还是没反应就看上位机控制台 —— 每条配置命令都会打印 `[cfg] CFG:... -> ble`（或 `udp` / `serial`）。如果没有这行、或显示 `NOTHING`，说明三条通道都没连上。
* **蓝牙显示已连接但没反应**：配置走蓝牙需要**含配置通道的固件（v15 及以上）**，去设备信息屏看版本。如果第一次写入报 `Insufficient Authentication`，说明设备还没绑定 —— 配对一次即可。
* **屏幕黑屏或显示错乱**：烧了**别的屏幕驱动**的固件。去设备信息屏确认这版固件期望什么，再按第 2 节烧对应的。
* **开机时旋钮行为异常**：把旋钮从一端拧到另一端一次，芯片会立刻学会物理极限，之后就 100% 准确。
* **程序读不到温度**：以**管理员身份**运行 BindDeck，Windows 需要提权才允许 *LibreHardwareMonitor* 读取主板和显卡传感器。

---

祝你玩得开心！如果这个项目对你有用，可以通过 [GitHub Sponsors 请作者喝杯咖啡](https://github.com/sponsors/SanX18) 支持他。
