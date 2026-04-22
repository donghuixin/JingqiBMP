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

// 20Hz 採樣，每次發送 1 筆，消息 ~15 bytes
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

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("System Starting...");

  // 1. 初始化 I2C
  pinMode(D4, INPUT_PULLUP);
  pinMode(D5, INPUT_PULLUP);
  Wire.begin();
  initSensor();

  // 2. 初始化藍牙（使用默認帶寬，不要用 BANDWIDTH_MAX）
  Bluefruit.begin();
  Bluefruit.Periph.setConnInterval(12, 40); // 15ms ~ 50ms（保守、相容）
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
  // ★ 初始化：只做感測器初始化，不發任何 BLE 數據
  // 因為此時網頁可能還沒完成 startNotifications() 訂閱
  // 在訂閱前寫入的數據會塞滿 HVN queue 且永遠不會排空
  if (triggerInit && Bluefruit.connected()) {
    triggerInit = false;
    delay(500);
    initSensor();
    // ★ 不在這裡發 BLE 消息！等收到 S 指令再發
    Serial.print("初始化完成，BLE buffer=");
    Serial.println(bleuart.availableForWrite());
  }

  // IIC 自查
  if (triggerIICCheck && Bluefruit.connected()) {
    triggerIICCheck = false;
    uint8_t error, address;
    int nDevices = 0;
    bool foundBMP581 = false;

    Serial.println("IIC Scan...");
    for (address = 1; address < 127; address++) {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      if (error == 0) {
        Serial.print("IIC device: 0x");
        Serial.println(address, HEX);
        // 只有在 buffer 有空間時才發
        char buf[16];
        snprintf(buf, sizeof(buf), "IIC:0x%02X\n", address);
        if (bleuart.availableForWrite() >= (int)strlen(buf)) {
          bleuart.print(buf);
          delay(50);
        }
        if (address == BMP581_ADDR) foundBMP581 = true;
        nDevices++;
      }
      delay(2);
    }

    const char* result = (nDevices == 0) ? "IIC:NONE\n" :
                         (foundBMP581)   ? "BMP:OK\n" : "BMP:ERR\n";
    if (bleuart.availableForWrite() >= (int)strlen(result)) {
      bleuart.print(result);
    }
    Serial.println(result);
  }

  // ================= 定時量測與發送 =================
  if (isSendingData && isBmpReady && Bluefruit.connected()) {
    if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
      lastSampleTime = millis();

      float pressure = bmp58x.readPressPa();
      String statusVal = (pressure > 0) ? "1" : "0";

      // 極簡消息: "B:99183|S:1\n" ≈ 15 bytes = 1 個 BLE notification
      char dataMsg[32];
      snprintf(dataMsg, sizeof(dataMsg), "B:%ld|S:%s\n", (long)pressure, statusVal.c_str());

      int avail = bleuart.availableForWrite();
      if (avail >= (int)strlen(dataMsg)) {
        bleuart.print(dataMsg);
        Serial.print("TX: ");
        Serial.print(dataMsg);
      } else {
        // 不跳過，等一下再試
        Serial.print("wait(avail=");
        Serial.print(avail);
        Serial.println(")");
      }
    }
  }

  delay(1);
}

// ================= Callbacks（只設標誌位，不寫 bleuart）=================

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
    Serial.print("收到 S，buffer=");
    Serial.println(bleuart.availableForWrite());
    isSendingData = true;
  } else if (cmd == "P") {
    isSendingData = false;
    Serial.println("收到 P (暫停)");
  } else if (cmd == "C") {
    Serial.println("收到 C (自查)");
    triggerIICCheck = true;
  }
}