# LiClock 原理图

源文件 `docs/Hardware/LiClock.epro2`，嘉立创 EDA 专业版 3.2.77。工程名 schematic1，图纸更新到 2026-02-09 14:50。这份导出只含原理图。

主控是 **U2 ESP32-WROOM-32D-N16**。图纸五张：主控、墨水屏、USB、电源、可选功能。同名网络标号跨图纸连通。只挂着一个引脚、又没有网络名的线不列入网络表。

同一份数据的结构化版本在 `docs/Hardware/liclock.json`。

悬空且图纸未标 NC 的引脚：U10.4 BYP（电源）。

## 引脚

U2 的模组引脚。网络名来自原理图上的网络标号。

| 模组脚 | 名称 | 网络 | 对端 |
| --- | --- | --- | --- |
| 1 | GND | GND | GND（67 端，见电源） |
| 2 | 3V3 | 3V3 | 3V3（24 端，见电源） |
| 3 | EN | EN | R1.2 100kΩ、SW1.1、Q3.3 C（USB） |
| 4 | SENSOR_VP（GPIO36） | — | 未连接，图纸标了 NC |
| 5 | SENSOR_VN（GPIO39） | BUTTON1 | SW4.1、R20.1 100kΩ |
| 6 | IO34（GPIO34） | BUTTON3 | SW4.T、R18.1 100kΩ |
| 7 | IO35（GPIO35） | BUTTON2 | SW4.2、R19.1 100kΩ |
| 8 | IO32（GPIO32） | — | 未连接，图纸标了 NC |
| 9 | IO33（GPIO33） | ADC | R2.1 100kΩ、R3.2 100kΩ |
| 10 | IO25（GPIO25） | RTC_IRQ | R17.1 10kΩ（可选功能）、U11.3 INT#/SQW（可选功能） |
| 11 | IO26（GPIO26） | CHARGING | U4.1 CHRG（电源）、R10.2 100kΩ（电源） |
| 12 | IO27（GPIO27） | SDVDD_CTRL | Q5.1 G（可选功能）、R16.1 100kΩ（可选功能） |
| 13 | IO14（GPIO14） | SD_CS | CARD1.2 CD/DAT3（可选功能） |
| 14 | IO12（GPIO12） | SD_MOSI | CARD1.3 CMD（可选功能） |
| 15 | GND | GND | GND（67 端，见电源） |
| 16 | IO13（GPIO13） | SD_SCLK | CARD1.5 CLX（可选功能） |
| 17 | SD2 | — | 未连接，图纸标了 NC |
| 18 | SD3 | — | 未连接，图纸标了 NC |
| 19 | CMD | — | 未连接，图纸标了 NC |
| 20 | CLK | — | 未连接，图纸标了 NC |
| 21 | SD0 | — | 未连接，图纸标了 NC |
| 22 | SD1 | — | 未连接，图纸标了 NC |
| 23 | IO15（GPIO15） | SD_MISO | CARD1.7 DAT0（可选功能） |
| 24 | IO2（GPIO2） | CARDDETECT | CARD1.CD（可选功能） |
| 25 | IO0（GPIO0） | GPIO0 | Q4.3 C（USB） |
| 26 | IO4（GPIO4） | EPD_DIN | FPC1.14（墨水屏） |
| 27 | IO16（GPIO16） | EPD_CLK | FPC1.13（墨水屏） |
| 28 | IO17（GPIO17） | EPD_CS | FPC1.12（墨水屏） |
| 29 | IO5（GPIO5） | EPD_DC | FPC1.11（墨水屏） |
| 30 | IO18（GPIO18） | EPD_RST | FPC1.10（墨水屏） |
| 31 | IO19（GPIO19） | EPD_BUSY | FPC1.9（墨水屏） |
| 32 | NC | — | 未连接，图纸标了 NC |
| 33 | IO21（GPIO21） | BUZZER | Q6.1 B（可选功能） |
| 34 | RXD0 | RXD | U6.21 TXD（USB） |
| 35 | TXD0 | TXD | U6.20 RXD（USB） |
| 36 | IO22（GPIO22） | SCL | U8.4 SCK（可选功能）、U9.3 SCL（可选功能）、U11.8 SCL（可选功能） |
| 37 | IO23（GPIO23） | SDA | U8.3 SDI（可选功能）、U9.4 SDA（可选功能）、U11.7 SDA（可选功能） |
| 38 | GND | GND | GND（67 端，见电源） |
| 39 | GND | GND | GND（67 端，见电源） |

## 电源

### +5V

电源，9 个端点。

- USB：C17.2 1uF、U6.7 VDD5、U6.8 VBUS、USB1.A4B9 VBUS、USB1.B4A9 VBUS
- 电源：D6.2 K、Q1.1 G、R6.1 100kΩ、U4.4 VCC

### BAT

电源，3 个端点。

- 电源：Q1.3 D、U4.3 BAT、U12.1

### VCC

电源，7 个端点。

- 主控：R2.2 100kΩ
- 电源：C10.2 10uF、D6.1 A、Q1.2 S、U10.1 IN、U10.3 EN
- 可选功能：Q6.3 C

### 3V3

电源，24 个端点。

- 主控：C16.1 100nF、R1.1 100kΩ、R15.2 10kΩ、U2.2 3V3
- 墨水屏：C5.1 100nF、C8.1 4.7uF、FPC1.15、FPC1.16、L2.1 68uH
- USB：U6.5 VIO
- 电源：C7.2 10uF、R10.1 100kΩ、U10.5 OUT
- 可选功能：C20.2 10uF、Q5.2 S、R16.2 100kΩ、R17.2 10kΩ、U8.2 CSB、U8.5 SDO、U8.6 VDDIO、U8.8 VDD、U9.2 VDD、U11.2 VCC、U11.4 RST#

### SD_VDD

电源，3 个端点。

- 可选功能：C19.2 10uF、CARD1.4 VDD、Q5.3 D

### GND

地，67 个端点。

- 主控：C16.2 100nF、R3.1 100kΩ、R18.2 100kΩ、R19.2 100kΩ、R20.2 100kΩ、SW1.3、SW4.3、SW4.4、SW4.5、SW4.6、U2.1 GND、U2.15 GND、U2.38 GND、U2.39 GND
- 墨水屏：C2.2 1uF、C3.2 1uF、C5.2 100nF、C6.2 1uF、C8.2 4.7uF、C9.2 1uF、C11.2 1uF、C12.2 1uF、C13.2 1uF、C14.2 1uF、C15.2 1uF、D1.1 K、FPC1.8、FPC1.17、FPC1.25、FPC1.26、R7.1 1kΩ、R8.1 470mΩ
- USB：C17.1 1uF、C18.2 1uF、R4.2 5.1kΩ、R5.2 5.1kΩ、U6.2 GND、U6.25 EP、USB1.1 SHELL、USB1.2 SHELL、USB1.3 SHELL、USB1.4 SHELL、USB1.A1B12 GND、USB1.B1A12 GND
- 电源：C7.1 10uF、C10.1 10uF、R6.2 100kΩ、R9.2 4.7kΩ、U4.2 GND、U10.2 GND、U12.2、U12.3、U12.4
- 可选功能：BUZZER1.2、BUZZER1.3、C19.1 10uF、C20.1 10uF、CARD1.6 VSS、CARD1.10、CARD1.11、CARD1.12、CARD1.13、U8.1 GND、U8.7 GND、U9.5 GND、U11.5 GND、U11.6 VBAT

## 网络

| 网络 | 连接 |
| --- | --- |
| ADC | R2.1 100kΩ（主控）、R3.2 100kΩ（主控）、U2.9 IO33（主控） |
| BUTTON1 | R20.1 100kΩ（主控）、SW4.1（主控）、U2.5 SENSOR_VN（主控） |
| BUTTON2 | R19.1 100kΩ（主控）、SW4.2（主控）、U2.7 IO35（主控） |
| BUTTON3 | R18.1 100kΩ（主控）、SW4.T（主控）、U2.6 IO34（主控） |
| BUZZER | U2.33 IO21（主控）、Q6.1 B（可选功能） |
| CARDDETECT | U2.24 IO2（主控）、CARD1.CD（可选功能） |
| CHARGING | U2.11 IO26（主控）、R10.2 100kΩ（电源）、U4.1 CHRG（电源） |
| DN | U6.4 UD-（USB）、USB1.A7 DN1（USB）、USB1.B7 DN2（USB） |
| DP | U6.3 UD+（USB）、USB1.A6 DP1（USB）、USB1.B6 DP2（USB） |
| DTR | Q4.2 E（USB）、R11.1 10kΩ（USB）、U6.23 DTR（USB） |
| EN | R1.2 100kΩ（主控）、SW1.1（主控）、U2.3 EN（主控）、Q3.3 C（USB） |
| EPD_BUSY | U2.31 IO19（主控）、FPC1.9（墨水屏） |
| EPD_CLK | U2.27 IO16（主控）、FPC1.13（墨水屏） |
| EPD_CS | U2.28 IO17（主控）、FPC1.12（墨水屏） |
| EPD_DC | U2.29 IO5（主控）、FPC1.11（墨水屏） |
| EPD_DIN | U2.26 IO4（主控）、FPC1.14（墨水屏） |
| EPD_RST | U2.30 IO18（主控）、FPC1.10（墨水屏） |
| GDR | FPC1.2（墨水屏）、Q2.1 G（墨水屏）、R7.2 1kΩ（墨水屏） |
| GPIO0 | U2.25 IO0（主控）、Q4.3 C（USB） |
| PREVGH | C12.1 1uF（墨水屏）、D4.1 K（墨水屏）、FPC1.21（墨水屏） |
| PREVGL | C14.1 1uF（墨水屏）、D3.2 A（墨水屏）、FPC1.23（墨水屏） |
| RESE | FPC1.3（墨水屏）、Q2.2 S（墨水屏）、R8.2 470mΩ（墨水屏） |
| RTC_IRQ | U2.10 IO25（主控）、R17.1 10kΩ（可选功能）、U11.3 INT#/SQW（可选功能） |
| RTS | Q3.2 E（USB）、R12.1 10kΩ（USB）、U6.19 RTS（USB） |
| RXD | U2.34 RXD0（主控）、U6.21 TXD（USB） |
| SCL | U2.36 IO22（主控）、U8.4 SCK（可选功能）、U9.3 SCL（可选功能）、U11.8 SCL（可选功能） |
| SDA | U2.37 IO23（主控）、U8.3 SDI（可选功能）、U9.4 SDA（可选功能）、U11.7 SDA（可选功能） |
| SDVDD_CTRL | U2.12 IO27（主控）、Q5.1 G（可选功能）、R16.1 100kΩ（可选功能） |
| SD_CS | U2.13 IO14（主控）、CARD1.2 CD/DAT3（可选功能） |
| SD_MISO | U2.23 IO15（主控）、CARD1.7 DAT0（可选功能） |
| SD_MOSI | U2.14 IO12（主控）、CARD1.3 CMD（可选功能） |
| SD_SCLK | U2.16 IO13（主控）、CARD1.5 CLX（可选功能） |
| TXD | U2.35 TXD0（主控）、U6.20 RXD（USB） |

## 未命名网络

这几段线没有网络标号，两端器件是直接连着的。

| 网络 | 图纸 | 连接 |
| --- | --- | --- |
| UNNAMED-1 | 主控 | R15.1 10kΩ、SW4.C |
| UNNAMED-10 | 墨水屏 | C4.2 100nF、D4.2 A、L2.2 68uH、Q2.3 D |
| UNNAMED-11 | USB | C18.1 1uF、U6.6 V3 |
| UNNAMED-12 | USB | Q3.1 B、R11.2 10kΩ |
| UNNAMED-13 | USB | Q4.1 B、R12.2 10kΩ |
| UNNAMED-14 | USB | R4.1 5.1kΩ、USB1.B5 CC2 |
| UNNAMED-15 | USB | R5.1 5.1kΩ、USB1.A5 CC1 |
| UNNAMED-16 | 电源 | R9.1 4.7kΩ、U4.5 PROG |
| UNNAMED-17 | 可选功能 | BUZZER1.1、Q6.2 E |
| UNNAMED-2 | 墨水屏 | C2.1 1uF、FPC1.4 |
| UNNAMED-3 | 墨水屏 | C3.1 1uF、FPC1.5 |
| UNNAMED-4 | 墨水屏 | C6.1 1uF、FPC1.18 |
| UNNAMED-5 | 墨水屏 | C9.1 1uF、FPC1.19 |
| UNNAMED-6 | 墨水屏 | C11.1 1uF、FPC1.20 |
| UNNAMED-7 | 墨水屏 | C13.1 1uF、FPC1.22 |
| UNNAMED-8 | 墨水屏 | C15.1 1uF、FPC1.24 |
| UNNAMED-9 | 墨水屏 | C4.1 100nF、D1.2 A、D3.1 K |

## 器件

### 主控

| 位号 | 型号 | 值 | 封装 | 立创料号 |
| --- | --- | --- | --- | --- |
| C16 | CC0603KRX7R9BB104 | 100nF | C0603 | C14663 |
| MP1 | 螺丝孔 | M2螺丝 | M2螺丝孔 |  |
| MP2 | 螺丝孔 | M2螺丝 | M2螺丝孔 |  |
| MP3 | 螺丝孔 | M2螺丝 | M2螺丝孔 |  |
| R1 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| R2 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| R3 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| R15 | 0603WAF1002T5E | 10kΩ | R0603 | C25804 |
| R18 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| R19 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| R20 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| SW1 | TS-1076S-A9B3 |  | SW-SMD_4P-L5.2-W5.2-P3.70-LS6.4 | C492952 |
| SW4 | TM-2023 | 3挡拨轮开关 | SW-SMD_TM-2023-1 | C318951 |
| U2 | ESP32-WROOM-32D-N16 |  | WIFIM-SMD_ESP32-WROOM-32D | C2973676 |

### 墨水屏

| 位号 | 型号 | 值 | 封装 | 立创料号 |
| --- | --- | --- | --- | --- |
| C2 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C3 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C4 | CC0603KRX7R9BB104 | 100nF | C0603 | C14663 |
| C5 | CC0603KRX7R9BB104 | 100nF | C0603 | C14663 |
| C6 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C8 | CL10A475KO8NNNC | 4.7uF | C0603 | C19666 |
| C9 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C11 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C12 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C13 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C14 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C15 | 0603B105K500NT | 1uF | C0603 | C90540 |
| D1 | MBR0530 | 30V 500mA 550mV@500mA | SOD-123_L2.7-W1.6-LS3.7-RD-1 | C699108 |
| D3 | MBR0530 | 30V 500mA 550mV@500mA | SOD-123_L2.7-W1.6-LS3.7-RD-1 | C699108 |
| D4 | MBR0530 | 30V 500mA 550mV@500mA | SOD-123_L2.7-W1.6-LS3.7-RD-1 | C699108 |
| FPC1 | AFC07-S24ECC-00 | 0.5mm P数:24 抽屉式 上接 | FPC-SMD_24P-L17.5-W5.4-P0.50 | C11092 |
| L2 | CMI321611J680KT | 68uH | L1206 | C394989 |
| Q2 | AO3402 | N沟道 30V 4A | SOT-23_L2.9-W1.3-P1.90-LS2.4-BR | C14385 |
| R7 | 0603WAF1001T5E | 1kΩ | R0603 | C21190 |
| R8 | CR0603FR470P05Z | 470mΩ | R0603 | C175657 |

### USB

| 位号 | 型号 | 值 | 封装 | 立创料号 |
| --- | --- | --- | --- | --- |
| C17 | 0603B105K500NT | 1uF | C0603 | C90540 |
| C18 | 0603B105K500NT | 1uF | C0603 | C90540 |
| Q3 | SS8050 | NPN 25V 1.5A Y1 200-350 | SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR | C2150 |
| Q4 | SS8050 | NPN 25V 1.5A Y1 200-350 | SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR | C2150 |
| R4 | 0603WAF5101T5E | 5.1kΩ | R0603 | C23186 |
| R5 | 0603WAF5101T5E | 5.1kΩ | R0603 | C23186 |
| R11 | 0603WAF1002T5E | 10kΩ | R0603 | C25804 |
| R12 | 0603WAF1002T5E | 10kΩ | R0603 | C25804 |
| U6 | CH9102F |  | QFN-24_L4.0-W4.0-P0.50-BL-EP2.5 | C2858418 |
| USB1 | TYPE-C 16PIN 3MD(385) |  | USB-C-SMD_TYPE-C16PIN | C2858270 |

### 电源

| 位号 | 型号 | 值 | 封装 | 立创料号 |
| --- | --- | --- | --- | --- |
| C7 | CL10A106MA8NRNC | 10uF | C0603 | C96446 |
| C10 | CL10A106MA8NRNC | 10uF | C0603 | C96446 |
| D6 | SS34-MS | 电压:40V 电流:3A | DO-214AC_L4.3-W2.7-LS5.0-RD | C2836396 |
| Q1 | SI2301CDS-T1-GE3 | P沟道 20V 3.1A | SOT-23-3_L3.0-W1.7-P0.95-LS2.9-BR | C10487 |
| R6 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| R9 | 0603WAF4701T5E | 4.7kΩ | R0603 | C23162 |
| R10 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| U4 | LTH7R. | 4054 | SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL | C841234 |
| U10 | SPX3819M5-L-3-3/TR | 输入16V 输出3.3V 500mA | SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL | C9055 |
| U12 | ZX-MX1.25-2PWT | 1x2P 间距:1.25mm 卧贴 系列:MX | CONN-SMD_2P-P1.25_MEGASTAR_ZX-MX1.25-2PWT | C7430468 |

### 可选功能

| 位号 | 型号 | 值 | 封装 | 立创料号 |
| --- | --- | --- | --- | --- |
| BUZZER1 | SMD-5025-0340-12Ω | 贴片无源蜂鸣器 | BUZ-SMD_3P-L5.0-W5.0-BL | C2693575 |
| C19 | CL10A106MA8NRNC | 10uF | C0603 | C96446 |
| C20 | CL10A106MA8NRNC | 10uF | C0603 | C96446 |
| CARD1 | TF PUSH | 自弹式TF卡座 TF外焊式内存卡卡座 | TF-SMD_TF-PUSH | C393941 |
| Q5 | SI2301CDS-T1-GE3 | P沟道 20V 3.1A | SOT-23-3_L3.0-W1.7-P0.95-LS2.9-BR | C10487 |
| Q6 | SS8050 | NPN 25V 1.5A Y1 200-350 | SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR | C2150 |
| R16 | 0603WAF1003T5E | 100kΩ | R0603 | C25803 |
| R17 | 0603WAF1002T5E | 10kΩ | R0603 | C25804 |
| U8 | BMP280 |  | LGA-8_L2.5-W2.0-P0.65_AMP6127 | C83291 |
| U9 | AHT20 |  | SENSOR-SMD_L3.0-W3.0-P1.00-BR | C2757850 |
| U11 | DS3231MZ+TRL | I2C 2.3V~5.5V | SOIC-8_L4.9-W3.9-P1.27-LS6.0-BL | C107410 |
