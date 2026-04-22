# JingQiBMP — BMP581 氣壓感測器 BLE 即時監控系統

基於 **Seeed XIAO nRF52840 (BLE)** 與 **BMP581 高精度氣壓感測器**的無線即時監控系統。固件透過 BLE (藍牙低功耗) 將氣壓數據串流至瀏覽器端 Web Dashboard，實現 **100Hz 採樣、20Hz 批次推送**的即時可視化。

> **線上 Dashboard**：推送到 GitHub 後，`index.html` 會自動部署至 GitHub Pages。  
> 訪問 `https://huixin.space/JingqiBMP/` 即可使用。

---

## 目錄

- [系統架構](#系統架構)
- [硬體需求](#硬體需求)
- [接線圖](#接線圖)
- [I2C 地址配置](#i2c-地址配置)
- [開發環境設置](#開發環境設置)
- [固件燒錄步驟](#固件燒錄步驟)
- [BLE 通訊協議](#ble-通訊協議)
- [採樣率與發送間隔設定](#採樣率與發送間隔設定)
- [BLE 吞吐量計算與參數調優](#ble-吞吐量計算與參數調優)
- [Web Dashboard 使用說明](#web-dashboard-使用說明)
- [故障排除](#故障排除)
- [檔案結構](#檔案結構)
- [授權條款](#授權條款)

---

## 系統架構

```
┌────────────────────┐     BLE (NUS)      ┌────────────────────┐
│  Seeed XIAO BLE    │ ◄──────────────── │  Chrome / Edge     │
│  nRF52840          │ ──────────────►   │  Web Bluetooth API │
│                    │   Notifications    │                    │
│  ┌──────────────┐  │                    │  ┌──────────────┐  │
│  │  BMP581      │  │                    │  │  Chart.js    │  │
│  │  I2C (0x47)  │  │                    │  │  Dashboard   │  │
│  └──────────────┘  │                    │  └──────────────┘  │
└────────────────────┘                    └────────────────────┘
```

**數據流程**：
1. BMP581 以 100Hz (每 10ms) 採集氣壓
2. 每 5 筆累積為一個批次 (每 50ms)
3. 透過 BLE Nordic UART Service (NUS) 以 Notification 推送至瀏覽器
4. Web Dashboard 即時解析、繪圖、顯示

---

## 硬體需求

| 元件 | 型號 | 說明 |
|------|------|------|
| 主控板 | [Seeed XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/) | 支援 BLE 5.0 的超小型開發板 |
| 氣壓感測器 | [BMP581](https://www.bosch-sensortec.com/products/environmental-sensors/pressure-sensors/bmp581/) | 高精度數字氣壓計，量程 30~125 kPa |
| 連接線 | 杜邦線 × 4 | VCC、GND、SDA、SCL |
| 電腦 | 任意帶 BLE 的電腦 | 需使用 Chrome 或 Edge 瀏覽器 |

---

## 接線圖

```
Seeed XIAO nRF52840          BMP581 模組
┌──────────────┐             ┌──────────┐
│          3V3 ├─────────────┤ VCC      │
│          GND ├─────────────┤ GND      │
│     D4 (SDA) ├─────────────┤ SDA      │
│     D5 (SCL) ├─────────────┤ SCL      │
│              │             │ SDO → 見下方 I2C 地址配置
└──────────────┘             └──────────┘
```

> **重要**：固件已對 D4、D5 開啟內部上拉電阻 (`INPUT_PULLUP`)。如果你的 BMP581 模組**自帶上拉電阻**（大部分模組都有），則無需外接。若 I2C 通訊不穩定，可嘗試外接 **4.7kΩ 上拉電阻**至 3.3V。

---

## I2C 地址配置

BMP581 支援兩個 I2C 地址，透過 **SDO 引腳**（或模組上的焊接跳線）選擇：

| SDO 引腳狀態 | I2C 地址 | 說明 |
|-------------|----------|------|
| **懸空 / 高電平** (預設) | `0x47` | 固件預設值 |
| **接地 (GND)** | `0x46` | 備用地址 |

### 修改 I2C 地址

如果你的 BMP581 模組 SDO 接地（使用 `0x46`），需修改固件中的地址常數：

```cpp
// JingqiBMP581.ino 第 6 行
const uint8_t BMP581_ADDR = 0x47;  // ← 改為 0x46
```

以及感測器實例化：

```cpp
// 第 7 行
DFRobot_BMP58X_I2C bmp58x(&Wire, BMP581_ADDR);
```

### DFRobot 模組特殊說明

DFRobot 的 Fermion / Gravity 系列 BMP581 模組使用**焊接跳線（Solder Pad）**而非外露 SDO 引腳：

- **焊盤斷開 (出廠預設)**：地址 = `0x47`
- **焊盤短接**：地址 = `0x46`

### 如何驗證 I2C 地址

連接設備後，在 Web Dashboard 中點擊 **「自查 (C)」** 按鈕，設備會掃描整個 I2C 總線並回報所有找到的設備地址。串口日誌也會同步輸出掃描結果。

---

## 開發環境設置

### 第一步：安裝 Arduino IDE

1. 下載並安裝 [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. 開啟 Arduino IDE

### 第二步：安裝 Seeed nRF52 開發板支援包

1. 打開 `File → Preferences`
2. 在 **Additional Boards Manager URLs** 中加入：
   ```
   https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
   ```
3. 打開 `Tools → Board → Boards Manager`
4. 搜尋 **Seeed nRF52** 並安裝 `Seeed nRF52 Boards`（版本 ≥ 1.1.8）

### 第三步：安裝 DFRobot BMP58X 函式庫

1. 打開 `Tools → Manage Libraries`
2. 搜尋 **DFRobot_BMP58X**
3. 安裝最新版本

或從 GitHub 手動安裝：
```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/DFRobot/DFRobot_BMP58X.git
```

### 第四步：選擇開發板

1. `Tools → Board`：選擇 **Seeed XIAO nRF52840**
2. `Tools → Port`：選擇對應的 COM 口（Windows）或 `/dev/ttyACMx`（Linux/Mac）
3. **不需要**修改其他設定（如 Bootloader、Softdevice 等，全部保持預設）

---

## 固件燒錄步驟

1. 用 USB-C 線連接 Seeed XIAO nRF52840 至電腦
2. 在 Arduino IDE 中打開 `JingqiBMP581.ino`
3. 確認開發板與 COM 口設置正確
4. 點擊 **Upload（上傳）** 按鈕
5. 等待編譯與燒錄完成
6. 打開 `Tools → Serial Monitor`，設定 **115200 baud**
7. 應看到如下輸出：
   ```
   System Starting...
   開始初始化 BMP581...
   BMP581 初始化成功 (0x47), 耗時: 13 ms
   藍牙廣播中，名稱：JingQiBMP。等待網頁連線...
   ```

> **若看到 "BMP581 初始化失敗"**，請檢查接線與 I2C 地址設定。

---

## BLE 通訊協議

### Service 與 Characteristic

本系統使用 **Nordic UART Service (NUS)** 進行雙向通訊：

| UUID | 名稱 | 方向 | 說明 |
|------|------|------|------|
| `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | NUS Service | — | 服務 UUID |
| `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | RX Characteristic | Web → 設備 | 發送指令 (Write) |
| `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | TX Characteristic | 設備 → Web | 接收數據 (Notification) |

### 指令表（Web → 設備）

| 指令 | 功能 | 說明 |
|------|------|------|
| `S` | 開始 (Start) | 開始氣壓數據串流 |
| `P` | 暫停 (Pause) | 暫停數據串流 |
| `C` | 自查 (Check) | 掃描 I2C 總線，檢查 BMP581 連接狀態 |

### 回應消息（設備 → Web）

#### 初始化消息
| 消息 | 含義 |
|------|------|
| `INIT:OK` | BMP581 初始化成功 |
| `INIT:FAIL` | BMP581 初始化失敗 |
| `SR:100Hz` | 採樣率為 100Hz |
| `RNG:30~125kPa` | 量程為 30~125 kPa |
| `READY` | 設備就緒 |
| `CMD:S/P/C` | 可用指令提示 |

#### 氣壓數據包
```
B:<val1>,<val2>,<val3>,<val4>,<val5>|S:<status>\n
```

| 欄位 | 說明 | 範例 |
|------|------|------|
| `B:` | 前綴，標識為氣壓數據 | — |
| `<val>` | 整數氣壓值 (Pa) | `99183` |
| `\|S:` | 分隔符 + 狀態前綴 | — |
| `<status>` | 感測器健康狀態：`1`=正常, `0`=異常 | `1` |

**範例**：`B:99183,99184,99183,99185,99183|S:1`

#### I2C 自查回應
| 消息 | 含義 |
|------|------|
| `IIC:SCAN` | 開始掃描 |
| `IIC:0x47` | 在地址 0x47 找到設備 |
| `IIC:NONE` | 未找到任何 I2C 設備 |
| `BMP:OK` | BMP581 連接正常 |
| `BMP:ERR` | BMP581 未找到 |

---

## 採樣率與發送間隔設定

### 當前設定

| 參數 | 值 | 說明 |
|------|-----|------|
| 採樣間隔 | 10 ms | 每 10ms 讀取一次 BMP581 (100Hz) |
| 批次大小 | 5 筆 | 每累積 5 筆發送一次 |
| 批次發送間隔 | 50 ms | = 10ms × 5，即每秒 20 次 BLE 推送 |
| 消息長度 | ~42 bytes | 5 個整數壓力值 + 狀態位 |

### 如何修改採樣率

修改 `JingqiBMP581.ino` 中的常數：

```cpp
const int SAMPLE_INTERVAL_MS = 10;  // 採樣間隔 (ms)
const int BATCH_SIZE = 5;           // 每批次的樣本數
```

#### 常見配置方案

| 方案 | 採樣間隔 | 批次大小 | 實際效果 | 適用場景 |
|------|---------|---------|---------|---------|
| **高速** | `10 ms` | `5` | 100Hz 採樣, 50ms/批, ~42 bytes | 需要高頻氣壓變化觀測 |
| **均衡** | `20 ms` | `5` | 50Hz 採樣, 100ms/批, ~42 bytes | 日常監控 |
| **省電** | `100 ms` | `10` | 10Hz 採樣, 1000ms/批, ~72 bytes | 長時間記錄 |
| **超省電** | `1000 ms` | `1` | 1Hz 採樣, 1000ms/批, ~14 bytes | 天氣站 |

> ⚠️ **注意**：修改批次大小後，需同步修改陣列宣告：
> ```cpp
> float pressureBuffer[BATCH_SIZE];  // 必須與 BATCH_SIZE 一致
> ```

---

## BLE 吞吐量計算與參數調優

### 吞吐量公式

```
最大吞吐量 = (MTU - 3) × HVN_Queue_Size / 連線間隔
```

### 預設參數下的計算

| 參數 | 預設值 | 說明 |
|------|--------|------|
| MTU | 23 bytes | 默認 BLE MTU |
| 有效 Payload | 20 bytes | MTU - 3 bytes ATT header |
| HVN Queue | 3 | Bluefruit 默認 notification queue 深度 |
| 連線間隔 | 15~30 ms | `setConnInterval(12, 24)` |

```
安全吞吐量 ≈ 20 bytes × 3 / 30ms ≈ 2,000 bytes/s
```

### 消息大小計算

```
"B:99183,99183,99183,99183,99183|S:1\n"
 = 2 + (5×5 + 4) + 4 + 1 + 1 = ~42 bytes

需要的 BLE Notification 數量 = ceil(42 / 20) = 3 個 ✓ (剛好等於 HVN Queue)
```

### 每秒數據量

```
42 bytes × 20 批/秒 = 840 bytes/s  (< 2000 bytes/s 安全吞吐量 ✓)
```

### 連線間隔設定

```cpp
Bluefruit.Periph.setConnInterval(12, 24); // 15ms ~ 30ms
```

| 參數 | 單位 | 範圍 | 預設 | 說明 |
|------|------|------|------|------|
| min | 1.25ms | 6~3200 | 12 (15ms) | 越小越快，越耗電 |
| max | 1.25ms | 6~3200 | 24 (30ms) | 越大越慢，越省電 |

#### 各場景建議

| 場景 | min | max | 效果 |
|------|-----|-----|------|
| 高速即時 | 6 | 12 | 7.5~15ms，最高吞吐量，但部分 BLE 適配器可能不支援 |
| **均衡 (推薦)** | **12** | **24** | **15~30ms，相容性與速度兼顧** |
| 省電 | 24 | 40 | 30~50ms，降低功耗 |
| 超省電 | 80 | 160 | 100~200ms，極低功耗 |

### ⚠️ 重要：不要使用 `BANDWIDTH_MAX`

```cpp
// ❌ 不要這樣做！會導致某些筆電 BLE 適配器不相容
Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

// ✅ 使用默認帶寬
Bluefruit.begin();  // 直接 begin，不設 bandwidth
```

**原因**：`BANDWIDTH_MAX` 會增大 HVN queue 和修改 ATT table 大小。某些筆記型電腦的 BLE 適配器（特別是 Intel、Realtek 方案）在 MTU 協商時會與這些參數不相容，導致 notification 無法送出，`availableForWrite()` 永遠返回 0。

---

## Web Dashboard 使用說明

### 瀏覽器要求

| 瀏覽器 | 支援 | 備註 |
|--------|------|------|
| Chrome | ✅ | 版本 ≥ 56 |
| Edge | ✅ | 基於 Chromium |
| Opera | ✅ | 基於 Chromium |
| Firefox | ❌ | 不支援 Web Bluetooth |
| Safari | ❌ | 不支援 Web Bluetooth |

> **必須使用 HTTPS 或 localhost**。Web Bluetooth API 要求安全上下文。
> GitHub Pages 自動提供 HTTPS。本地測試可用 `npx serve` 或直接開啟 `index.html`（Chrome 允許 `file://` 使用 Web Bluetooth）。

### 操作流程

1. 打開 Dashboard 頁面
2. 點擊 **「連線設備 (Connect)」**
3. 在彈出的配對視窗中選擇 **JingQiBMP**
4. 等待設備就緒（日誌中出現 `READY`）
5. 點擊 **「開始 (S)」** 開始即時數據串流
6. 圖表會即時繪製氣壓曲線
7. 點擊 **「暫停 (P)」** 暫停串流
8. 點擊 **「自查 (C)」** 檢查 I2C 設備連接

### Dashboard 功能

| 區域 | 功能 |
|------|------|
| 狀態徽章 | 顯示 BLE 連線狀態 |
| 初始化狀態面板 | 顯示 BMP581 初始化結果、採樣率、量程 |
| 即時壓力值 | 大字體顯示最新氣壓讀數 (Pa) |
| 壓力趨勢圖 | 即時折線圖，最多顯示 500 個數據點 (約 25 秒) |
| 設備日誌 | 終端風格的通訊日誌，顯示所有收發消息 |

---

## 故障排除

### 固件問題

| 症狀 | 可能原因 | 解決方案 |
|------|---------|---------|
| "BMP581 初始化失敗" | 接線錯誤 / I2C 地址不對 | 檢查 SDA/SCL 接線，確認 I2C 地址 |
| 串口無輸出 | COM 口錯誤 / Baud rate 不對 | 確認 COM 口，設定 115200 baud |
| `⚠ BLE滿 跳過` | BLE 吞吐量不足 | 增大批次間隔或減小批次大小 |
| 連線後立刻斷開 | 連線間隔過小 | 增大 `setConnInterval` 參數 |

### Web Dashboard 問題

| 症狀 | 可能原因 | 解決方案 |
|------|---------|---------|
| 看不到設備列表 | 藍牙未開啟 / 不支援的瀏覽器 | 開啟系統藍牙，使用 Chrome |
| 連線成功但無數據 | 未點擊「開始」 | 點擊「開始 (S)」按鈕 |
| 數據停止 | BLE 緩衝區溢出 | 重新連線，或降低發送頻率 |
| "連線失敗" | HTTPS 要求 | 確保使用 HTTPS 或 localhost |

### I2C 調試

使用 **「自查 (C)」** 功能可以快速診斷 I2C 問題：
- 如果回報 `IIC:NONE` → I2C 總線上無設備，檢查接線
- 如果回報 `IIC:0x46` 但固件設定 `0x47` → 修改固件中的地址
- 如果回報 `BMP:OK` 但數據異常 → 檢查 BMP581 是否損壞

---

## 檔案結構

```
JingqiBMP581/
├── JingqiBMP581.ino        # Arduino 固件主程式
├── index.html              # Web Dashboard (單檔案, 含 CSS + JS)
├── README.md               # 本文檔
└── .github/
    └── workflows/
        └── static.yml      # GitHub Pages 自動部署 CI
```

### 自動部署

本專案已配置 GitHub Actions，每次推送到 `main` 分支時，`index.html` 會自動部署到 GitHub Pages。部署完成後可透過以下網址訪問：

```
https://donghuixin.github.io/JingqiBMP/
```

---

## 授權條款

MIT License — 自由使用、修改與分發。
