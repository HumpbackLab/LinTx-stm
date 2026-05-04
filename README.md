# LinTx-stm

这是一个基于 STM32G030 的 LinTx 输入前端固件。

当前固件负责在 STM32 侧采样本地输入，并通过 UART 按 `0x5A` 输入帧格式发送给 LinTx，校验方式为 CRC-8/DVB-S2。

## 当前状态

- 目前这份固件已经支持摇杆硬件路径。
- UART 输入协议已经切换到扩展帧，包含 `SW`、`SH` 和 `buttons` 字段。
- 由于当前这份工程里只接入了 4 路模拟量采样，所以 `SW`、`SH` 和 `buttons` 现在仍然固定发送 `0`。
- 你另外焊接的“纯按键版本”硬件目前还没有并入这份固件，它的 GPIO 采样和按键映射后续可以在独立分支里继续改。

## 硬件信息

### MCU

- `STM32G030F6Px`

### 当前已启用引脚

- `PA0` -> `ADC1_IN0` -> `CH1`
- `PA1` -> `ADC1_IN1` -> `CH2`
- `PA2` -> `ADC1_IN2` -> `CH3`
- `PA3` -> `ADC1_IN3` -> `CH4`
- `PB6` -> `USART1_TX`
- `PB7` -> `USART1_RX`
- `PC14` -> 状态灯
- `PA13` -> `SWDIO`
- `PA14` -> `SWCLK`

### 采样与传输

- ADC 触发源：`TIM3 TRGO`
- UART：`USART1`，`115200 8N1`
- ADC 扫描通道数：4
- ADC 传输方式：DMA 循环模式

按当前定时器配置（`64 MHz / 64 / 2000`），ADC 触发频率约为 `500 Hz`。

## UART 输入报文

当前固件发送的是扩展输入包：

```text
5A 0E 01 CH1_L CH1_H CH2_L CH2_H CH3_L CH3_H CH4_L CH4_H SW SH BTN_L BTN_H CRC
```

字段说明：

- `0x5A`：同步字节
- `0x0E`：负载长度
- `0x01`：输入消息类型
- `CH1..CH4`：4 路模拟通道，`u16` 小端
- `SW`：4 个前面板三档开关打包后的 1 字节
- `SH`：2 个肩键二档开关打包后的 1 字节
- `BTN_L BTN_H`：`buttons` 按位打包后的 `u16`
- `CRC`：只对负载字节做 CRC-8/DVB-S2，不包含 CRC 本身

当前只有 `CH1..CH4` 是实时数据，`SW`、`SH`、`buttons` 仍然是补零发送。

## 运行行为

- 上电后，STM32 先等待串口收到 ASCII 命令 `start_stm`
- 进入工作态后，每次 ADC 转换完成就发送一帧输入数据
- 收到 `sleep_stm` 后回到空闲态
- 空闲态下状态灯快闪，工作态下状态灯按固定周期翻转

## 构建方法

本工程使用 CMake Presets，生成器为 Ninja。

配置：

```powershell
cmake --preset Debug
```

编译：

```powershell
cmake --build build/Debug
```

## 仓库说明

- 当前本地目录已经初始化为 git 仓库，默认分支为 `main`
- 远端 `origin` 已设置为 `git@github.com:HumpbackLab/LinTx-stm.git`
- 之前查询远端时没有拿到 refs，现阶段更像是空仓库或尚未初始化完成

## 后续计划

- 为纯按键硬件版本补 GPIO 采样逻辑
- 将开关和五向键状态映射到 `SW`、`SH`、`buttons`
- 保持同一套协议，同时兼容摇杆板和按键板
