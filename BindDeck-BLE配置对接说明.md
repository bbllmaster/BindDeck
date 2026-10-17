# BindDeck BLE 配置对接说明

本文档面向**上位机 / PC 端**开发者，说明 BindDeck 通过 BLE 与上位机通信的协议，
重点是按钮上报消息 `BTN:` 的格式，以及**多设备**场景下如何区分按键来源。

BindDeck 是一台 8 按键 + 1 编码器的蓝牙 HID 键盘。它通过两条通道与上位机交互：

1. **HID 通道**：把每个按键映射成标准 HID 键码（如 `F13`~`F24`）发给操作系统，
   操作系统再把这些键码投递给当前聚焦的应用。这是“即插即用”的通道，但**无法
   区分来自哪台设备**（见下方“多设备注意事项”）。
2. **侧通道（BTN: 消息）**：通过 BLE 的 CFG/CMD 特征值（TX notify）把“哪个物理
   按钮被按下”以文本形式上报给上位机。上位机可订阅该特征值，按设备 + 按钮做精确
   映射，不受操作系统焦点影响。

---

## 一、BLE 设备名（设备识别）

每台设备上电后会广播一个**基于 MAC 的唯一 BLE 设备名**，格式为：

```
BindDeck-XXXX
```

其中 `XXXX` 是该设备 WiFi/BLE 基 MAC 地址最后两字节的十六进制大写表示
（例如 `BindDeck-A1B2`）。同一台设备的 WiFi 热点名（`BindDeck-XXXX`）与 BLE 名
后缀一致，方便在扫描 / 配对时区分多台设备。

> 上位机在扫描阶段即可用 BLE 名区分设备；配对后做按键映射时，再以 `BTN:` 消息里的
> `devId` 字段为准（二者后缀相同）。

---

## 二、BTN: 消息格式

按钮 / 编码器被按下或旋转时，固件通过 BLE TX 通知发送如下文本消息：

```
BTN:<devId>:<btn>
```

| 字段    | 含义                                                         |
| ------- | ------------------------------------------------------------ |
| `devId` | 设备短标识，等于 BLE 名后缀（MAC 末两字节，4 位十六进制大写，如 `A1B2`） |
| `btn`   | 按钮编号：`0`~`7` 对应 8 个物理按键，`8` 对应编码器按钮      |

### 示例

- 设备 `BindDeck-A1B2` 按下第 0 个按键：
  ```
  BTN:A1B2:0
  ```
- 设备 `BindDeck-3F8C` 按下第 7 个按键：
  ```
  BTN:3F8C:7
  ```
- 设备 `BindDeck-A1B2` 按下编码器按钮：
  ```
  BTN:A1B2:8
  ```

### 旧格式兼容说明（破坏性变更）

本功能上线前，`BTN:` 消息格式为 `BTN:<btn>`（不含 `devId`）。上线后改为
`BTN:<devId>:<btn>`。**上位机需同步升级解析逻辑**：按 `devId` 分组，再按 `btn`
映射到具体动作。单台设备场景下，旧的单设备用法依旧可用，仅报文多了一个 `devId`
段（解析时忽略或按单设备处理即可）。

---

## 三、多设备：BTN 消息含设备 ID 及示例

当**多台 BindDeck** 同时连接上位机时（操作系统会识别为多个独立 BLE HID 键盘），
单靠 HID 键码**无法**区分某个 `F13` 来自设备 A 还是设备 B。此时应依赖本侧的
`BTN:` 通道做精确映射：

1. 订阅每台设备 BLE 的 CFG/CMD 特征值通知。
2. 收到 `BTN:<devId>:<btn>` 时，用 `devId` 区分物理设备，`btn` 区分按键。
3. 映射表形如：
   - 设备 A（`A1B2`）按钮 0 → 运行程序 X
   - 设备 B（`3F8C`）按钮 0 → 打开文件 Y
   （即使 A、B 都把按钮 0 映射成同一个 HID 键码 `F13`，上位机仍能通过 `devId`
   区分并触发各自不同的动作。）

### 多设备注意事项

> **不要只依赖 HID 键码做多设备区分。** 在纯 HID 通路下，多台设备会发送相同的
> 键码（如都发 `F13`），来源无法区分。多设备区分必须依赖 `BTN:` 侧通道的设备 ID。

BLE 配置服务 UUID（`b1d3c0de-0001/0002/0003`）跨设备重复是合法的（它标识服务
类型而非实例），因此每台设备可独立配置，互不影响。

---

## 四、固件产物（合并固件 / OTA / 分区修复）

三种二进制均由 `platformio` 构建产物经 `esptool` 合成，构建环境见 `platformio.ini`
（5 个环境：`esp32dev` / `esp32dev_sh1106` / `esp32dev_sh1107` /
`esp32c3` / `esp32c3_ssd1306`）。

- **OTA 固件（app-only）**：` .pio/build/<env>/firmware.bin`，即仅应用分区镜像，
  用于网页 / PC 端 OTA 升级（`pc_script/firmware.bin` 为 esp32dev 的 OTA 镜像）。
- **合并固件（merged）**：用 `esptool.py merge_bin` 将 bootloader + 分区表 + app
  (+ phy_init) 合成单一可烧录镜像：
  ```bash
  esptool.py --chip <esp32|esp32c3> merge_bin \
    -o firmware_merged.bin \
    0x1000 .pio/build/<env>/bootloader.bin \
    0x8000 .pio/build/<env>/partitions.bin \
    0x10000 .pio/build/<env>/firmware.bin
  # 经典 ESP32 还需 0xe000 .pio/build/<env>/phy_init.bin（C3 不需要）
  ```
- **分区修复（partition-repair）**：单独烧写分区表即可修复损坏的分区表，
  ` .pio/build/<env>/partitions.bin` 直接 `write_flash 0x8000 partitions.bin`。

> 注意：合并固件包含分区表，是“整片烧录”镜像；若设备已存在但分区表异常（NVS
> 缺失 / 损坏），优先用分区修复镜像或重新烧写合并固件，而非仅 OTA app-only 镜像。
