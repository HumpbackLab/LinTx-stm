# LinTx-stm

这是一个基于 `STM32G030` 的 LinTx 输入前端固件。

当前分支为纯按键版本，STM32 负责轮询本地按键和开关状态，并通过 `USART2` 按扩展输入帧格式发送给 LinTx。CRC 使用 `CRC-8/DVB-S2`。

## 当前状态

- 当前实现为纯按键板，不采集 ADC 模拟量。
- 发送协议已经使用扩展帧，包含 `SW`、`SH`、`buttons` 字段。
- `CH1..CH4` 当前固定发送中位值 `2048`，用于兼容接收端仍然存在的四路模拟通道字段。
- `TIM1` 以 `100 Hz` 产生周期中断，中断中只置采样标志，主循环收到标志后再读取 GPIO、组包、发送。
- `USART2` 保留 `start_stm` / `sleep_stm` 串口命令控制。

## 硬件信息

### MCU

- `STM32G030F6Px`

### 当前引脚映射

#### 五向键

- `PB7` -> `BTN1_UP`
- `PC14` -> `BTN1_DOWN`
- `PC15` -> `BTN1_LEFT`
- `PA0` -> `BTN1_RIGHT`
- `PA1` -> `BTN1_MID`

#### 三段开关

- `PA11` -> `SB_LO`
- `PA12` -> `SB_HI`
- `PA7` -> `SC_HO`
- `PB0` -> `SC_HI`

#### 调试与通信

- `PA2` -> `USART2_TX`
- `PA3` -> `USART2_RX`
- `PA13` -> `SWDIO`
- `PA14` -> `SWCLK`

### 输入电气约定

- 五向键公共端接地，GPIO 使用内部上拉，按下时读到低电平。
- 两个三段开关公共端接地，GPIO 使用内部上拉。
- 三段开关采用两根输入线描述一个三段状态：
  - `LO=0, HI=1` -> 位置 `0`
  - `LO=1, HI=1` -> 位置 `1`
  - `LO=1, HI=0` -> 位置 `2`
  - `LO=0, HI=0` -> 视为非法组合，不输出错误报文，保持上一次合法状态继续发送

## 采样与发送

- 采样节拍：`TIM1`，`100 Hz`
- 串口：`USART2`，`115200 8N1`
- 输入方式：GPIO 轮询
- 发送方式：主循环检测采样标志后组包并发送

`TIM1` 当前配置为：

```text
64 MHz / (6399 + 1) / (99 + 1) = 100 Hz
```

## UART 输入报文

当前固件发送扩展输入包：

```text
5A 0E 01 CH1_L CH1_H CH2_L CH2_H CH3_L CH3_H CH4_L CH4_H SW SH BTN_L BTN_H CRC
```

字段说明：

- `0x5A`：同步字节
- `0x0E`：负载长度
- `0x01`：输入消息类型
- `CH1..CH4`：4 路模拟通道，`u16` 小端
- `SW`：三段开关打包字节
- `SH`：肩键/二段开关字节，当前固定为 `0`
- `BTN_L BTN_H`：五向键打包后的 `u16`
- `CRC`：仅对负载字节做 `CRC-8/DVB-S2`

### 当前映射

#### `buttons`

当前五向键映射到协议中的第 3 组五向键：

- bit10 = `BTN1_UP`
- bit11 = `BTN1_DOWN`
- bit12 = `BTN1_LEFT`
- bit13 = `BTN1_RIGHT`
- bit14 = `BTN1_MID`

#### `sw`

- bit0-bit1 = `SB_LO/SB_HI`
- bit2-bit3 = `SC_HO/SC_HI`

#### `sh`

- 当前固定为 `0`

## 运行行为

- 上电后等待串口命令 `start_stm`
- 进入工作态后，每次 `TIM1` 周期到达，主循环发送一帧输入数据
- 收到 `sleep_stm` 后停止发送并回到等待态

## 构建

本工程使用 CMake Presets，生成器为 Ninja。

配置：

```powershell
cmake --preset Debug
```

编译：

```powershell
cmake --build build/Debug
```

## 后续方向

- 将当前纯按键输入路径与未来的 ADC+摇杆路径合并为统一报文发送框架
- 保留 GPIO 轮询读取按键/开关
- 在需要时重新接回 `TIM3 -> ADC -> DMA` 的摇杆采样链路
- 若后续切换到引脚更多的 MCU，可直接增加更多开关、按键或 ADC 通道，现有协议结构也更容易继续扩展
