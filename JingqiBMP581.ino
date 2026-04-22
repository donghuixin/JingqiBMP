#include <Wire.h>
#include <bluefruit.h>
#include "DFRobot_BMP58X.h"

// ================= 硬體與感測器設定 =================
const uint8_t BMP581_ADDR = 0x47;
DFRobot_BMP58X_I2C bmp58x(&Wire, BMP581_ADDR);
bool isBmpReady = false;

// ================= 藍牙 BLE 設定 =================
BLEUart bleuart;

// ================= 狀態控制變數 =================
bool isSendingData = false;
unsigned long lastSampleTime = 0;
float lastPressure = 0;
int stuckCounter = 0;

// 20Hz 採樣，每次發送 1 筆
const int SAMPLE_INTERVAL_MS = 50;  // 50ms = 20Hz

bool triggerInit = false;
bool triggerIICCheck = false;

// ================= 初始化感測器 =================
void initSensor() {
  unsigned long startT = millis();
  Serial.println("開始初始化 BMP581...");

  if (bmp58x.begin()) {
    isBmpReady = true;
    bmp58x.setMeasureMode(bmp58x.eNormal);
    unsigned long duration = millis() - startT;
    Serial.print("BMP581 初始化成功 (0x47), 耗時: ");
    Serial.print(duration);
    Serial.println(" ms");
  } else {
    isBmpReady = false;
    Serial.println("BMP581 初始化失敗！請檢查硬體。");
  }
}

void setupWDT() {
  NRF_WDT->CONFIG = 0x01;       // 即使在 Sleep 模式下也運行 WDT
  NRF_WDT->CRV = 32768 * 3;     // 設置 3 秒超時 (32768 ticks = 1 second)
  NRF_WDT->RREN = 0x01;         // 啟用 Reload Register 0
  NRF_WDT->TASKS_START = 1;     // 啟動 WDT
}

void feedWDT() {
  NRF_WDT->RR[0] = 0x6E524635;  // 餵狗 (特定魔術數字)
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("System Starting...");

  // 0. 啟動看門狗 (3秒超時)
  setupWDT();

  // 1. 初始化 I2C
  pinMode(D4, INPUT_PULLUP);
  pinMode(D5, INPUT_PULLUP);
  Wire.begin();
  initSensor();

  // 2. 初始化藍牙
  Bluefruit.begin();
  Bluefruit.Periph.setConnInterval(12, 40); // 15ms ~ 50ms
  Bluefruit.setTxPower(4);
  Bluefruit.setName("JingQiBMP");

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // 3. 初始化 BLE UART
  bleuart.begin();
  bleuart.setRxCallback(rx_callback);

  // 4. 開始廣播
  startAdv();
  Serial.println("藍牙廣播中，名稱：JingQiBMP。等待連線...");
}

void startAdv() {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void loop() {
  // ★ 餵狗：只要 loop 正常運行，就不會重啟
  feedWDT();

  // ★ 初始化：只做感測器初始化，不發任何 BLE 數據
  if (triggerInit && Bluefruit.connected()) {
    triggerInit = false;
    delay(500);
    initSensor();
  }

  // IIC 自查
  if (triggerIICCheck && Bluefruit.connected()) {
    triggerIICCheck = false;

    Serial.println("IIC Scan...");
    uint8_t error, address;
    int nDevices = 0;
    bool foundBMP581 = false;

    for (address = 1; address < 127; address++) {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      if (error == 0) {
        Serial.print("IIC: 0x");
        Serial.println(address, HEX);

        // ★ 直接用 bleuart.print()，不檢查 availableForWrite()
        // 因為 BLEUart 沒有實作 availableForWrite()，
        // 它繼承的 Stream 基類永遠回傳 0（這就是 buffer=0 的原因！）
        // bleuart.write() 內部已有 notifyEnabled() 檢查
        char buf[16];
        snprintf(buf, sizeof(buf), "IIC:0x%02X\n", address);
        bleuart.print(buf);
        delay(100); // 讓 BLE 協議棧有時間處理

        if (address == BMP581_ADDR) foundBMP581 = true;
        nDevices++;
      }
      delay(2);
    }

    if (nDevices == 0) {
      bleuart.print("IIC:NONE\n");
    } else if (foundBMP581) {
      bleuart.print("BMP:OK\n");
    } else {
      bleuart.print("BMP:ERR\n");
    }
    Serial.println(foundBMP581 ? "BMP:OK" : "BMP:ERR");
  }

  // ================= 定時量測與發送 =================
  if (isSendingData && isBmpReady && Bluefruit.connected()) {
    if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
      lastSampleTime = millis();

      float pressure = bmp58x.readPressPa();

      // ★ 動態健康檢查：檢測數值是否卡死
      if (pressure == lastPressure) {
        stuckCounter++;
      } else {
        stuckCounter = 0; // 有變化就清零
        lastPressure = pressure;
      }

      // 如果連續 10 次數值一模一樣 (代表感測器死機或庫函數出錯)
      if (stuckCounter >= 10) {
        Serial.println("偵測到傳感器卡死！嘗試重新喚醒...");
        bleuart.print("SYS: Sensor Stuck! Restarting...\n");
        
        // 強制重新初始化傳感器
        initSensor();
        stuckCounter = 0;
        lastPressure = 0;
        return; // 跳出這次循環，等下一次
      }

      // ★ 關鍵修正：直接用 bleuart.print() 發送！
      // 
      // 之前的錯誤：用 bleuart.availableForWrite() 做流控
      // 但 BLEUart 類別沒有覆寫 availableForWrite()，
      // 它繼承自 Arduino Stream 基類，默認返回 0。
      // 所以「buffer=0」不是 BLE 緩衝區滿，而是函數根本沒實作！
      //
      // bleuart.write() 內部（BLEUart.cpp:250）已經有：
      //   if (!notifyEnabled(conn_hdl)) return 0;
      // 所以如果 CCCD 未訂閱，write 會安全地跳過，不會阻塞。
      //
      // 而 BLECharacteristic::notify() 內部會用 SoftDevice 的
      // sd_ble_gatts_hvx() 來發送，有 HVN queue 管理。

      char dataMsg[32];
      snprintf(dataMsg, sizeof(dataMsg), "B:%ld|S:1\n", (long)pressure);
      bleuart.print(dataMsg);

      Serial.print("TX: ");
      Serial.print(dataMsg);
    }
  }

  delay(1);
}

// ================= Callbacks（只設標誌位）=================

void connect_callback(uint16_t conn_handle) {
  char central_name[32] = { 0 };
  BLEConnection* connection = Bluefruit.Connection(conn_handle);
  connection->getPeerName(central_name, sizeof(central_name));
  Serial.print("已連線至設備: ");
  Serial.println(central_name);

  triggerInit = true;
  isSendingData = false;
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle; (void)reason;
  Serial.println("已斷線，重新廣播...");
  isSendingData = false;
}

void rx_callback(uint16_t conn_handle) {
  (void)conn_handle;
  String cmd = "";
  while (bleuart.available()) {
    cmd += (char)bleuart.read();
  }
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "S") {
    isSendingData = true;
    // 打印 notifyEnabled 的真實狀態做診斷
    Serial.print("收到 S，notifyEnabled=");
    Serial.println(bleuart.notifyEnabled() ? "YES" : "NO");
  } else if (cmd == "P") {
    isSendingData = false;
    Serial.println("收到 P (暫停)");
  } else if (cmd == "C") {
    Serial.println("收到 C (自查)");
    triggerIICCheck = true;
  }
}