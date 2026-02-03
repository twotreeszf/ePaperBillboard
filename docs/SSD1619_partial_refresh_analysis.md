# SSD1619 局部刷新实现分析

## 最终实现小结（lib/GxEPD2_420_SSD1619）

- **局刷窗口**：`_setPartialRamArea()` 使用 0x11 0x03、0x44（X 起/止）、0x45（Y 起/止）、0x4e/0x4f（RAM 指针），与数据手册及 EPaperDrive OPM42 一致。
- **局刷 LUT**：首次局刷时发送 0x21 0x00，再 0x32 + 70 字节 `LUT_PARTIAL_SSD1619`（源于 OPM42 `LUTDefault_part_opm42`），第一组 TP 取 0x0c,0x0c,0x00,0x0c,0x01 以减轻残影。
- **局刷触发**：每次局刷发 **0x22 0xC7**（Enable Clock + ANALOG → DISPLAY，不 Load OTP LUT）再 **0x20**，然后等待 BUSY。
- **状态**：`_partial_lut_loaded` 标记局刷 LUT 是否已加载；全刷（`_Update_Full`）或关电（`_PowerOff`）后置 false，下次局刷会重新加载 LUT。
- **效果**：局刷可用，有轻微残影；应用层建议定期全刷（如 TTLvglEpdDriver 的 `EPD_FULL_REFRESH_INTERVAL`）以清累积残影。

---

## 〇、SSD1619A 数据手册（PDF）局刷要点

依据 **SSD1619A Rev 0.10** 数据手册，局刷支持与实现要点如下。

### 0.1 芯片对局刷的声明

- **Features**（§2）：明确列出 **“Support display partial update”**。
- 局刷通过 **RAM 窗口 + 写 RAM + 显示更新序列** 实现，无单独“局刷命令”，即：先限定窗口，再写该窗口数据，最后执行显示更新。

### 0.2 与局刷相关的命令（Command Table §7）

| 命令 | 说明 | 局刷用途 |
|------|------|----------|
| **0x11** | Data Entry mode setting | 设定地址递增/递减方向（AM、ID[1:0]），窗口内写 RAM 时地址按此更新。 |
| **0x44** | Set RAM X-address Start/End | X 方向窗口：XSA[5:0]、XEA[5:0]，**单位是 8 像素**（0x00–0x31 对应 400 源）。 |
| **0x45** | Set RAM Y-address Start/End | Y 方向窗口：YSA[8:0]、YEA[8:0]，**单位是 1 行**（0x000–0x12B 对应 300 行）。 |
| **0x4E** | Set RAM X address counter | 写 RAM 前设置 X 地址指针 XAD[5:0]。 |
| **0x4F** | Set RAM Y address counter | 写 RAM 前设置 Y 地址指针 YAD[8:0]（2 字节：低 8 位 + 高 1 位）。 |
| **0x24** | Write RAM (BW) | 之后写入的数据进入 B/W RAM，地址按 0x11 与窗口自动更新。 |
| **0x26** | Write RAM (RED) | 同上，写入 RED RAM。 |
| **0x20** | Master Activation | **启动显示更新序列**；更新选项由 **0x22** 决定；操作期间 BUSY 为高。 |
| **0x21** | Display Update Control 1 | RAM 内容选项（Normal/Bypass/Inverse），[POR]=0x00。 |
| **0x22** | Display Update Control 2 | **显示更新序列选项**：是否使能时钟/模拟、是否 Load LUT、DISPLAY Mode 1/2 等。 |
| **0x32** | Write LUT register | 从 MCU 写入 **70 字节** LUT（VS[nX-LUT]、TP[nX]、RP[n]），用于自定义波形（含局刷波形）。 |

### 0.3 命令说明中的局刷约束（§8.4–8.6）

- **0x44**：X 窗口以 **8 倍地址单位** 指定，即 X 以 8 像素为单位；需在写 RAM 前设置；00h ≤ XSA,XEA ≤ 31h。
- **0x45**：Y 窗口以 **1 个地址单位** 指定；00h ≤ YSA,YEA ≤ 12Bh（300 行）。
- **0x4E/0x4F**：地址计数器初值 **必须落在 0x44/0x45 定义的窗口内**，且符合 Data Entry（0x11）的 AM、ID 设置，**否则会出现异常显示**。

因此局刷正确做法是：**先 0x44/0x45 设局刷窗口 → 再 0x4E/0x4F 设写指针（通常在窗口内起始点）→ 0x24/0x26 写数据 → 0x22 + 0x20 执行更新**。

### 0.4 波形与 LUT（§6.7、§7 中 0x32）

- **Waveform Setting (WS)** 共 **76 字节**：前 **70 字节** 为 LUT（VS、TP、RP），由 **0x32** 写入；后 6 字节对应 0x03、0x04、0x3A、0x3B 等寄存器。
- 局刷若使用**与全刷不同的波形**（更短、更少 phase），需在局刷前通过 **0x21 + 0x32** 切到“局刷 LUT”，再执行 0x22 + 0x20。

### 0.5 典型全屏显示序列（§9.1）与局刷对应关系

全屏序列为：

1. 0x11（Data Entry）→ 0x44（X 起/止）→ 0x45（Y 起/止）→ 0x4E（X 指针）→ 0x4F（Y 指针）
2. 0x24 或 0x26 写 RAM
3. 0x22（Display Update Control 2）→ 0x20（Master Activation）

**局刷**即把上述 0x44/0x45 设为**局部矩形**，0x4E/0x4F 设为该矩形内起始地址，只写该矩形对应的 RAM 数据，再 0x22 + 0x20。数据手册未单独给出“局刷流程图”，局刷 = 窗口化写 RAM + 同一套显示更新（可选不同 0x22 或不同 LUT）。

### 0.6 为 SSD1619 添加局刷支持的实现要点（基于数据手册）

1. **窗口与指针**  
   - 使用 **0x44**：Xstart/8, Xend/8（2 字节）。  
   - 使用 **0x45**：Ystart、Yend 各 2 字节（低 8 位 + 高 1 位），即 (y%256, y/256)。  
   - 使用 **0x4E**：X 起始地址（x/8）。  
   - 使用 **0x4F**：Y 起始地址（y 低 8 位、y 高 1 位）。  
   - 0x11 的 AM、ID 要与写顺序一致（例如 Y 递减时指针通常设为 Yend，再写至 Ystart）。

2. **显示更新**  
   - 局刷仍用 **0x20**（Master Activation）；行为由 **0x22** 决定。  
   - 若希望“只做 DISPLAY、不重新 Load LUT”，可采用 0x22 中仅 DISPLAY 的选项（如 0x04/0x0C 等，见 Table 7-1），避免用 OTP 全序列覆盖已通过 0x32 写入的局刷 LUT。

3. **局刷 LUT（可选但推荐）**  
   - 若屏厂/参考代码提供局刷专用 LUT，应在进入局刷前：**0x21 0x00**（Display Update Control 1）后，用 **0x32** 写入 **70 字节** 局刷 LUT，再在每次局刷时 0x44/0x45/0x4E/0x4F → 0x24 → 0x22 → 0x20。

4. **RAM 地址与数据量**  
   - X 方向每“格”为 8 像素，每行字节数 = (Xend - Xstart + 1)；Y 方向行数 = (Yend - Ystart + 1)。写 0x24 的数据量 = 每行字节数 × 行数。

### 0.7 从全刷波形能否分析出局刷波形？

**结论**：数据手册给出了波形的**结构**和**编码方式**，可以从全刷波形**推断局刷的修改方向**，但**无法仅凭手册推导出精确的局刷 LUT 字节**；具体数值依赖屏厂/经验（如 EPaperDrive 的 OPM42 局刷 LUT）。

#### 0.7.1 数据手册中的 LUT 结构（§6.6、§6.7，Figure 6-6）

- **0x32 写入 70 字节**，对应 WS byte 0~69：
  - **前 35 字节**：电压选择 **VS[nX-LUT]**。  
    - 7 组 × 4 相 = 28 相，每相对应 LUT0~LUT4 共 5 个 2-bit 电压（00=VSS, 01=VSH1, 10=VSL, 11=VSH2）。  
    - 按 EPaperDrive 的排布，可理解为 **5 个 LUT 各 7 字节**（L0 黑 / L1 白 / L2 红 / L3 红 / L4 VCOM），每字节编码若干相的电压。
  - **后 35 字节**：**TP[0A], TP[0B], TP[0C], TP[0D], RP[0], … , TP[6D], RP[6]**。  
    - 7 组 × (4 个相位长度 TP + 1 个重复次数 RP) = 35 字节。  
    - TP[nX]=0 表示该相跳过；RP[n]=0 表示该组执行 1 次。

因此：**局刷 LUT 与全刷 LUT 共用同一 70 字节结构**，差异只在各字节的取值。

#### 0.7.2 全刷 vs 局刷（OPM42 示例）对比

| 区域 | 全刷 LUTDefault_full_opm42 | 局刷 LUTDefault_part_opm42 | 说明 |
|------|-----------------------------|-----------------------------|------|
| **L0 (黑)** | 0x08,0x00,0x48,0x40,0x00,0x00,0x00 | 0x00,0x00,0x00,0x00,0x00,0x00,0x00 | 局刷 L0 多为 VSS，少驱动 |
| **L1 (白)** | 0x20,0x00,0x12,0x20,0x00,0x00,0x00 | 0x82,0x00,0x00,0x00,0x00,0x00,0x00 | 局刷仅少量相有电压 |
| **L2 (白→黑)** | 0x00,... | 0x50,0x00,... | 局刷用更短过渡 |
| **TP/RP 段** | 0x05,0x20,0x20,0x05,0x00；0x0f,0x00,... | 0x08,0x08,0x00,0x08,0x01（本驱动采用 0x0c,0x0c,0x00,0x0c,0x01 减轻残影）；余 0 | 局刷 TP 更小、多组 RP 为 0 |

可归纳为：

1. **VS 段（前 35 字节）**：局刷常把大量相设为 **00（VSS）**，只保留少数相有 VSH1/VSL/VSH2，即**更短、更简单的电压序列**。
2. **TP/RP 段（后 35 字节）**：局刷的 **TP 普遍更小**（相位更短），**RP 多为 0 或 1**（少重复），整体波形时间更短。

#### 0.7.3 能从全刷“推导”出的内容

- **结构**：局刷 LUT 与全刷一样，均为 70 字节，前 35 字节 VS、后 35 字节 TP/RP；可直接复用同一写入流程（0x32 + 70 字节）。
- **修改方向**（用于尝试性构造局刷 LUT）：
  1. **缩短相位**：将全刷 LUT 中 TP[nA]~TP[nD] 按比例缩小或改为较小常数（如 0x05→0x02），注意 TP=0 表示跳过该相。
  2. **减少重复**：将 RP[n] 设为 0 或 1，减少组重复次数。
  3. **简化电压序列**：将不需要的相对应的 VS 置为 00（VSS），仅保留必要的一两相驱动（如 L1 只保留一个 0x82 这样的过渡）。

这样得到的是一套“可能可用”的局刷波形，**是否最优需上屏验证**。

#### 0.7.4 无法仅凭手册得出的内容

- **精确字节值**：数据手册**没有**给出“局刷 LUT 公式”或“partial = f(full)”；全刷 LUT 通常来自 OTP/屏厂，局刷 LUT 多为屏厂或参考代码（如 EPaperDrive）针对具体屏型号**经验调优**的结果。
- **面板差异**：不同 400×300 屏（即便同是 SSD1619）可能有不同 VCOM、电容、材料，同一套“由全刷推导的局刷”在不同屏上表现可能差异很大（残影、闪烁、均匀性）。

因此：**可以**从全刷波形**分析出局刷波形的结构和修改思路**，并据此尝试构造局刷 LUT；**不能**单靠手册**唯一确定**局刷 LUT 的每个字节。工程上建议：若屏型号对应 OPM42，直接采用 `LUTDefault_part_opm42`；若为其他屏，可先按 0.7.3 由全刷做一版“缩短版”局刷 LUT，再结合实际效果或屏厂资料微调。

---

下文参考 `demo/EPaperDrive-main`（**OPM42 = SSD1619**，WF42 = UC8176）与数据手册，对照 `lib/GxEPD2_420_SSD1619` 的最终实现与排查要点。

---

## 一、EPaperDrive 中 OPM42（SSD1619）局部刷新流程（应参考）

### 1. 局刷初始化 `EPD_init_Part()`（约 2127–2133 行）

- 先调用 `EPD_Init()` 做基础初始化。
- **0x21**、**0x00**（Display Update Control）。
- 写入局刷 LUT：`LUTDefault_part_opm42`（0x32 命令，在 `WAVEFORM_SETTING_LUT.h`）。

### 2. 设置局部窗口 `EPD_SetRamArea()`（约 1423–1436 行，OPM42 与 WX29/GDEY042Z98 同分支）

对 OPM42（与 WX29、GDEY042Z98 等同一分支）：

- **0x44**：Xstart/8, Xend/8（2 字节）。
- **0x45**：Ystart, Ystart1, Yend, Yend1（4 字节，Y 为 16bit 拆成低/高）。
- 无 0x91/0x90。

随后 `EPD_SetRamPointer(xStart/8, yEnd%256, yEnd/256)`：**0x4e**（X）、**0x4f**（Y 低、Y 高）。

### 3. 写显示 RAM

- `EPD_WriteDispRam()` 写 0x24 到控制器。

### 4. 执行局部刷新 `EPD_Update_Part()`（约 1993–1996 行，OPM42）

对 OPM42（及 DKE42_3COLOR）：

- 仅发送 **0x20**（Activate Display Update Sequence），无 0x92/0x12。

### 5. 完整局刷调用 `EPD_Dis_Part()`（OPM42 分支 2513–2549 行）

顺序为：

1. 坐标变换后 `EPD_SetRamArea(...)`（0x44 + 0x45）。
2. `EPD_SetRamPointer(...)`（0x4e + 0x4f）。
3. `EPD_WriteDispRam(...)` 写新图像（0x24）。
4. `EPD_Update_Part()`（仅 0x20）。
5. `ReadBusy_long()` 两次。
6. OPM42 再写一次 `EPD_WriteDispRam`（同一 buffer），不写 0x26 旧数据。

---

## 二、EPaperDrive 中 WF42（UC8176）局部刷新流程（非 SSD1619，仅作对比）

- WF42 使用 **0x91 + 0x90** 设窗口、**0x92 + 0x12** 触发刷新，以及 `lut_part_wf42_*`（0x20–0x24）。控制器为 UC8176，与 SSD1619 命令集不同，此处不展开。

---

## 三、当前项目中的实现（最终）

### 1. GxEPD2_420_SSD1619（lib）

- **局部窗口** `_setPartialRamArea()`：
  - **0x11 0x03**、**0x44**（X 起/止，按 8 像素）、**0x45**（Y 起/止，各 2 字节）、**0x4e/0x4f**（RAM 指针），与数据手册及 OPM42 一致。
- **局部刷新** `_Update_Part()`：
  - 若 `!_partial_lut_loaded`：先 **0x21 0x00**，再 **0x32** + 70 字节局刷 LUT（`LUT_PARTIAL_SSD1619`），并置 `_partial_lut_loaded = true`。
  - 每次局刷：**0x22 0xC7**（Enable Clock + ANALOG → DISPLAY，不重新 Load OTP LUT）→ **0x20** → 等待 BUSY。
- **局刷 LUT** `LUT_PARTIAL_SSD1619`：70 字节，源于 EPaperDrive OPM42 `LUTDefault_part_opm42`；第一组 TP 设为 **0x0c,0x0c,0x00,0x0c,0x01**（相对原 0x08 略增，减轻残影）。
- **全刷 / 关电**：`_Update_Full()` 与 `_PowerOff()` 中置 `_partial_lut_loaded = false`，下次局刷会重新加载局刷 LUT。
- **初始化** `_InitDisplay()`：基础 init + 全屏 `_setPartialRamArea`，不预写局刷 LUT（在首次局刷时写入）。

### 2. TTLvglEpdDriver

- 已正确使用 `setPartialWindow(x1, y1, w, h)` 和 `firstPage()` / `nextPage()`。
- 通过 `drawPixel()` 把 LVGL 的 `px_map` 画到 GxEPD2 的 `_buffer`，再由 `nextPage()` 里 `writeImage(_buffer, ...)` + `refresh(x,y,w,h)` 下发并刷新，数据路径正确。
- 每 `EPD_FULL_REFRESH_INTERVAL` 次做一次全刷以防残影，逻辑合理。

因此，若局部刷新异常（残影重、不更新、花屏），更可能是**控制器命令与 LUT 与 EPaperDrive 不一致**，而不是 TTLvglEpdDriver 的调用方式。

---

## 四、与 EPaperDrive OPM42 的对应关系（最终）

| 项目           | EPaperDrive OPM42 (SSD1619) | GxEPD2_420_SSD1619（最终） |
|----------------|-----------------------------|-----------------------------|
| 局部窗口       | 0x44 (X起/止), 0x45 (Y起/止) | 0x11 0x03, 0x44, 0x45, 0x4e, 0x4f（一致） |
| RAM 指针       | 0x4e, 0x4f                  | 0x4e, 0x4f（一致）         |
| 局刷 LUT       | LUTDefault_part_opm42，0x21 0x00 后 0x32 写入 | LUT_PARTIAL_SSD1619（同源），首次局刷时 0x21 0x00 + 0x32 |
| 局刷 LUT 微调  | —                           | 第一组 TP 0x0c,0x0c,0x00,0x0c（减轻残影） |
| 局部刷新触发   | 仅 **0x20**                 | **0x22 0xC7** + **0x20**（0xC7 使能 Clock+ANALOG 再 DISPLAY，不 Load OTP） |
| 数据写 RAM     | 0x24                        | 0x24（一致）               |

---

## 五、实现要点与排查（参考）

- **0x22 必须显式发送**：POR 为 0xFF，若不发 0x22 则 0x20 会走完整序列并从 OTP Load LUT，局刷会像全刷。最终采用 **0x22 0xC7**（Enable Clock + ANALOG + DISPLAY，不 Load OTP）。
- **0x04/0x44 不足**：仅 DISPLAY 或仅 Enable ANALOG 时，本屏局刷无反应，需 0xC7 使能时钟与模拟后再 DISPLAY。
- **首次刷新**：GxEPD2 要求首次 refresh 为全刷（`_initial_refresh`），应用层需先做一次 `clearScreen()` 或 `refresh(false)` 再做局刷。

---

## 六、局刷异常时的排查

### 6.1 局刷像全刷或屏幕无反应

- **0x22**：POR 为 0xFF 会走完整序列并 Load OTP LUT，局刷会像全刷；**0x04** 仅 DISPLAY、不使能模拟，会导致无反应。最终采用 **0x22 0xC7**。
- **局刷 LUT**：若怀疑 LUT 与屏不匹配，可临时用全刷 LUT（70 字节）经 0x32 写入后局刷，验证通道是否正常，再恢复局刷 LUT 并微调 TP。

### 6.2 首次刷新必须为全刷（_initial_refresh）

- GxEPD2 中 `refresh(x, y, w, h)` 开头有：`if (_initial_refresh) return refresh(false);`，即第一次 refresh 会强制全刷。
- **用法**：先做一次全刷（`clearScreen(0xFF)` 或 `refresh(false)`），再做 `writeImage(...)` + `refresh(x, y, w, h)` 局刷。

---

## 七、减轻局刷残影

- **定期全刷**：在应用层每隔 N 次局刷做一次全刷（如 `refresh(false)` 或 `clearScreen` 再画整屏），可清掉累积残影。例如 TTLvglEpdDriver 的 `EPD_FULL_REFRESH_INTERVAL` 控制该间隔，可适当调小（更频繁全刷）以减轻残影。
- **局刷 LUT 相位（TP）**：驱动中局刷 LUT 的第一组 TP 已由 0x08 调整为 0x0c，略延长有效相位时间，有利于过渡更充分、减轻残影。若仍觉残影明显，可尝试再略增（如 0x0e），或适当增大 `partial_refresh_time` 以配合波形时长。
- **权衡**：TP 越大、局刷越慢、残影通常越轻；TP 过小则可能残影加重或局刷无反应。按屏实际观感在 LUT 与全刷间隔之间折中即可。
