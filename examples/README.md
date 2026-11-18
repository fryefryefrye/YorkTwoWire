# Examples

Minimal, self-contained example sketches (ESP8266 / Arduino) for the York YCWA16NCWQ H-Link two-wire bus. Each folder is a plain Arduino sketch (`.ino` in the folder root) that can be opened directly in the Arduino IDE, and also builds with PlatformIO (`pio run -t upload` in the folder).

| Example | Interface | What it does |
| :--- | :--- | :--- |
| [01_hlink_bus_monitor](01_hlink_bus_monitor/) | H-Link RX line (strong-power board ↔ weak-power board) | **RX only**: decodes all checksum-validated frames (18B settings / 39B status / 48B config / ACK) and prints the running AC state: power, mode, set temperature, room temperature, compressor, blower, fan level |
| [02_ir_control](02_ir_control/) | IR LED on D2 | Sends the HITACHI_AC3 184-bit state frame captured from the real remote: power, mode, set temperature, fan - the panel is the bus setting authority, so control goes over IR ("pressing a key for the panel") |

The two examples combine into one board (the deployed adapter: D6 = bus RX, D2 = IR TX, D5 = optional IR receiver for self-check / code learning) — exactly what the current York adapter firmware does: listen to the bus for status, control through the IR LED.

Protocol details, field tables, timing and evidence: see the main [README](../README.zh-CN.md).

## 中文说明

约克 YCWA16NCWQ 线控器 H-Link 两线总线的最简示例代码（ESP8266 / Arduino）。每个目录都是普通 Arduino 工程（`.ino` 在目录根下），可直接用 Arduino IDE 打开，也可用 PlatformIO 在目录内 `pio run -t upload` 编译上传。

| 例程 | 接口 | 功能 |
| :--- | :--- | :--- |
| [01_hlink_bus_monitor](01_hlink_bus_monitor/) | H-Link RX 线（强电板 ↔ 弱电板之间） | **只收不发**：解码全部校验通过的帧（18B 设置帧 / 39B 状态帧 / 48B 配置块 / ACK），打印空调运行状态：开关、模式、设定温度、室温、压缩机、内风机、风速档位 |
| [02_ir_control](02_ir_control/) | D2 红外发射 | 发送从真实遥控器学到的 HITACHI_AC3 184 位全状态帧：开关、模式、设定温度、风档——面板是总线设定权威，控制一律走红外（等价"人按按键"） |

两个例程可以合并到同一块板上（实际部署的适配板正是如此：D6 = 总线接收，D2 = 红外发射，D5 = 可选红外接收用于自检/补码）——监听总线获知状态、红外发射实现控制。协议细节、字段表、时序与证据文件见主 [README](../README.zh-CN.md)。
