# Example 02 - York AC Infrared Control (HITACHI_AC3 184-bit State Frame)

Sends the York panel's own infrared command - the **184-bit HITACHI_AC3 "Normal" state frame** captured from the real remote - so the AC can be controlled without touching the bus. The panel is the setting authority on the H-Link bus; IR control equals "a person pressing a key": the panel state changes first, then the panel forwards everything to the indoor unit over the bus and shows it on the display.

One command carries **all** parameters (power + mode + temperature + fan), built from "value + inverted value" complement pairs.

## Hardware

| Item | Notes |
| :--- | :--- |
| ESP8266 (NodeMCU) | any ESP8266 board |
| IR LED (940 nm) | on **D2 (GPIO4)**, driven directly through 100 Ω (peak ~15-20 mA at 50% duty), proven to reach the panel |
| IRremoteESP8266 library | 2.8.6 pinned in `platformio.ini`; in the Arduino IDE install it via the Library Manager |

## Serial Commands (115200 baud)

| Key | Action |
| :--- | :--- |
| `p` | toggle power |
| `c` / `d` / `f` / `h` | cool / dry / fan / heat (heat auto-jumps the setpoint to 20 °C, like the real panel) |
| `+` / `-` | set temperature 16..31 °C (panel clamps to 19..30 °C) |
| `1` / `2` / `3` | fan max / mid / min (the panel has no auto fan level) |

## Frame Structure (23 bytes)

| Field | Bytes | Encoding |
| :--- | :--- | :--- |
| Fixed header / body | [0]..[14], [19], [20] | `C0 0C 00 02 FD FF 00 67 98 91 6E 80 7F FC 03` ... `80 7F` |
| Temperature | [15] / [16] | code / ~code, table of all 16 codes (16..31 °C) in the sketch |
| Mode × fan | [17] / [18] | `(mode << 4 \| fan)` / ~byte17; mode: heat=1 dry=2 fan=4 cool=6, fan: max=0x3 mid=0x5 min=0x9 |
| Power | [21] / [22] | ON = `09` / OFF = `05`, byte22 = ~byte21 |

Timing (38 kHz carrier, matches `kHitachiAc3` library constants): header 3400/1660 µs, bit mark 460 µs, space 0 = 410 µs / 1 = 1250 µs, **footer mark 460 µs** + 100 ms gap.

## The Two Pitfalls (read before debugging)

1. **Bit order**: the remote is MSB-first, `sendHitachiAc3()` is LSB-first. This sketch stores every byte **pre-reversed (Rev8)** so the wire stream equals the remote's. Equivalent alternative: feed the original remote bytes to `IRsend::sendGeneric(..., MSBfirst = true)`.
2. **Footer mark**: 372 edges = header 2 + 184 bits × 2 + footer mark 1 + final gap. Without the footer mark, all data bits are correct and the panel still drops the frame. Also send every command **twice** (receiver AGC).

Never validate bit order by "we sent it, we received it" - that is circular. Compare against a real remote capture or the panel's reaction.

## 中文说明

对约克面板发红外命令——即从真实遥控器学习到的 **184 位 HITACHI_AC3 "Normal" 全状态帧**（23 字节，38kHz，MSB 先发）。面板是 H-Link 总线的设定权威，红外控制等价"人按按键"：面板自身状态先变，再由它经总线下发给内机并上屏，总线"覆盖"问题从根上消失。

- 发射：D2(GPIO4) 串 100Ω 直驱红外管（不用 MOS，峰值 15~20mA，实测够用）。
- 一条命令携带全部参数（电源+模式+温度+风档），全帧为"值+取反"互补对结构；码表（16~31℃ 全 16 个温度码、模式/风档半字节）在代码内。
- **两个坑**（见代码头注释与上文）：位序（遥控器 MSB 先发、库 LSB 先发 → 码表逐位预反转）、帧尾 mark（372 边沿的收尾条件，缺了面板弃帧）。每条命令连发两遍（AGC）。
- 串口 115200 输入：`p` 开关机、`c/d/f/h` 模式、`+/-` 温度、`1/2/3` 最大/中/最小风。
