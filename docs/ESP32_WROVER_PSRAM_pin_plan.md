# 换用带 PSRAM 的 ESP32 模组改线方案

当前原理图模组是 **ESP32-WROOM-32D-N16**（无 PSRAM）。若换成外置 PSRAM 的 **WROVER**（如 WROVER-E），模块内部已把 **IO16、IO17** 接给 PSRAM，不能再外接。

固件里这两只脚正是墨水屏 SPI：

| 宏 | 现脚 | 信号 |
|---|---|---|
| `TT_UI_EPD_SCK` | IO16 | EPD_CLK |
| `TT_UI_EPD_CS` | IO17 | EPD_CS |

定义在 `src/Tasks/TTUITask.h`。

## 改线

原理图上能输出、且目前空着的脚只有 **IO32**。RTC 中断在 **IO25**，是输入，改接到空着的 **SENSOR_VP（GPIO36）**。GPIO36 只能输入，适合中断，不能做 SPI。

| 信号 | 现在 | 改到 |
|---|---|---|
| EPD_CLK | IO16 | IO32 |
| EPD_CS | IO17 | IO25 |
| RTC_IRQ | IO25 | GPIO36（SENSOR_VP） |
| IO16、IO17 | 墨水屏 | 不外接 |

GPIO36 没有内部上拉。DS3231 的中断是开漏输出，这根线要另加一颗上拉电阻。

不要把时钟或片选改到 GPIO0、GPIO2、GPIO12、GPIO15。这几只是启动 strapping 脚。SD 卡、蜂鸣器、按键、I2C、串口保持不动。

WROVER 与 WROOM-32D 同为 38 脚，IO16/IO17 挪走后可以原位替换。

## 固件

改 `src/Tasks/TTUITask.h`：

```c
#define TT_UI_EPD_SCK   32
#define TT_UI_EPD_CS    25
```

`platformio.ini` 里现在是 `-D BOARD_HAS_PSRAM=0`，换模组后改为启用 PSRAM。当前分区是 8MB Flash（`partitions_8MB.csv`），模组 Flash 至少 8MB，例如 WROVER-E 的 N8R8 或 N16R8。N4（4MB Flash）放不下现有分区。

## 不改线的情况

片内封了 2MB PSRAM 的 **ESP32-D0WDR2-V3** 不占用 IO16/IO17，墨水屏接线可以不动。常见的 WROVER-E 是外置 PSRAM，仍占用这两只脚，必须按上表改线。
