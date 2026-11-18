# Example 01 - York H-Link Bus Status Monitor (RX Only)

Listens on the **RX line between the York power & modem board and the control panel** (panel side) and prints every checksum-validated H-Link frame plus the running AC state: power, mode, set temperature, room temperature, compressor, blower and fan level.

The sketch **never transmits on the bus** - listening is safe and complete, and control goes over infrared (see [Example 02](../02_ir_control/)).

## Hardware

| Item | Notes |
| :--- | :--- |
| ESP8266 (NodeMCU) | any ESP8266 board |
| Access point | between the power & modem board and the control panel: the adapter is soldered to 3 wires (`+5V / GND / RX`) |

Power for the ESP8266 comes from the soldered `+5V` (through a 3.3 V regulator on the NodeMCU).

## Wiring

| Signal | ESP8266 pin |
| :--- | :--- |
| RX line (echo line, all frames visible) | D6 (GPIO12), 9600 baud 8E1 software serial |
| +5V | 5 V / VU |
| GND | GND |

## What You Will See (115200 baud)

```
18B panel->indoor  power=ON mode=Cool set=24C
ACK indoor->panel
39B indoor->panel  power=ON mode=Cool set=24C room=26C compressor=RUN blower=RUN fan=3
48B panel->indoor  config block changeFlag=0x00
[AC] power=ON mode=Cool set=24C room=26C compressor=RUN blower=RUN fan=3
[AC change] set 24->20C | mode Cool->Heat
```

- Frames are only printed after the checksum gate accepts them (`chk = XOR(byte[1]..byte[n-2])`); corrupted fragments are dropped silently with byte-wise re-synchronization.
- Steady state is idle: one heartbeat transaction (18B + 39B + 48B + 48B with 4 ACKs) every ~30 s, more traffic only after key presses on the panel.
- `[AC change]` lists exactly which fields changed - fan / compressor start-stop, setpoint edits, everything.

## Field Tables

See the main [README](../../README.zh-CN.md), section 3 (message format, checksum, ACK, frame types, full field table and frame samples).

## Build & Upload

- **Arduino IDE**: install the ESP8266 core, open `01_hlink_bus_monitor.ino`, select your board and upload.
- **PlatformIO**: `pio run -t upload` in this folder (see `platformio.ini`).

## 中文说明

监听约克线控面板**供电与调制解调板与控制面板之间的 RX 线**，打印所有通过校验和验证的 H-Link 帧，并解析空调运行状态（开关/模式/设定温度/室温/压缩机/内风机/风速档位）。

- 本例程**只收不发**：监听安全且完整，控制走红外（见例程 02）。
- 接线：焊接 3 根线——RX → D6(GPIO12)，9600 波特 8E1 软串；+5V → 5V；GND → GND。
- RX 线上有 TX 的物理回显：面板（0x21）与内机（0x12）的帧都出现在这一条线上，一路软串口全收。
- 串口 115200 输出逐帧内容与 `[AC]` 状态行、`[AC change]` 变化行；帧格式、校验和（XOR）、字段表见主 README 第三节。
