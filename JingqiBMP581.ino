#include <Wire.h>
#include <bluefruit.h>
#include "DFRobot_BMP58X.h"

// ================= 硬體與感測器設定 =================
const uint8_t BMP581_ADDR = 0x47;
DFRobot_BMP58X_I2C bmp58x(&Wire, BMP581_ADDR);
bool isBmpReady = false;

// ================= 藍牙 BLE 設定 (Bluefruit) =================
// 建立一個 Nordic UART Service (NUS) 的實例
BLEUart bleuart; 

// 狀態控制變數
bool isSendingData = false;
unsigned long lastSendTime = 0;
const int SEND_INTERVAL_MS = 1000;

void setup() {
  Serial.begin(115200);
  delay(2000); // 讓系統啟動穩定

  Serial.println("System Starting...");

  // 1. 初始化 I2C 與 BMP581 (開啟 D4, D5 上拉)
  pinMode(D4, INPUT_PULLUP);
  pinMode(D5, INPUT_PULLUP);
  Wire.begin();

  if (bmp58x.begin()) {
    isBmpReady = true;
    bmp58x.setMeasureMode(bmp58x.eNormal);
    Serial.println("BMP581 初始化成功 (0x47)");
  } else {
    isBmpReady = false;
    Serial.println("BMP581 初始化失敗！請檢查硬體。");
  }

  // 2. 初始化 Bluefruit 藍牙
  Bluefruit.begin();
  Bluefruit.setTxPower(4);    // 增加藍牙發射功率，提升穩定度
  Bluefruit.setName("JingQiBMP"); // 設定藍牙名稱

  // 設定連線與斷線的 Callback 函數 (事件驅動)
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // 3. 初始化 BLE UART 服務
  bleuart.begin();
  // 設定收到數據時的 Callback
  bleuart.setRxCallback(rx_callback);

  // 4. 設定與開始廣播 (Advertising)
  startAdv();
  Serial.println("藍牙廣播中，名稱：JingQiBMP。等待網頁連線...");
}

// ================= 廣播設定副程式 =================
void startAdv() {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart); // 將 UART 服務加入廣播包
  Bluefruit.ScanResponse.addName();          // 在掃描回應中加入名稱

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // 廣播間隔設定
  Bluefruit.Advertising.setFastTimeout(30);   // 前 30 秒快速廣播
  Bluefruit.Advertising.start(0);             // 0 = 持續廣播不停止
}

void loop() {
  // 在 Bluefruit 架構下，收發處理已由 Callback 完成
  // 主迴圈只要專心處理「定時量測與發送資料」即可
  if (isSendingData && isBmpReady && Bluefruit.connected()) {
    if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
      lastSendTime = millis();
      
      float pressure = bmp58x.readPressPa();
      
      // 組合資料字串，並加上換行符號方便網頁端讀取
      String dataMsg = "Press: " + String(pressure) + " Pa\n";
      
      // 透過藍牙 UART 發送
      bleuart.print(dataMsg);
      
      Serial.print("藍牙發送: ");
      Serial.print(dataMsg);
    }
  }
}

// ================= 事件回呼函數 (Callbacks) =================

// 當上位機(網頁)連上線時觸發
void connect_callback(uint16_t conn_handle) {
  // 取得連線設備名稱 (如果不支援則為空)
  char central_name[32] = { 0 };
  BLEConnection* connection = Bluefruit.Connection(conn_handle);
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("已連線至設備: ");
  Serial.println(central_name);

  // 稍微等待，確保通訊通道建立完成
  delay(500); 

  // 輸出 BMP 參數
  if (isBmpReady) {
    // BLEUart 可以像 Serial 一樣直接用 println
    bleuart.println("BMP Ready");
    bleuart.println("- IIC Address: 0x47");
    bleuart.println("- Range: 30kPa ~ 125kPa");
    bleuart.println("- Mode: Normal");
    bleuart.println("Send 'S' to start, 'P' to pause.");
    Serial.println("已向網頁發送 Ready 訊息與參數");
  } else {
    bleuart.println("Error: BMP581 Not Found at 0x47!");
  }
  
  isSendingData = false; // 預設不發送，等待 S 指令
}

// 當設備斷線時觸發
void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;
  Serial.println("網頁已斷線，重新開始廣播...");
  isSendingData = false; // 斷線自動停止發送
}

// 當收到上位機(網頁)訊息時觸發
void rx_callback(uint16_t conn_handle) {
  (void) conn_handle;
  
  String cmd = "";
  // 讀取緩衝區內所有收到的字元
  while (bleuart.available()) {
    cmd += (char)bleuart.read();
  }
  
  cmd.trim(); // 移除換行符號或空白
  cmd.toUpperCase(); // 轉大寫比對

  if (cmd == "S") {
    isSendingData = true;
    bleuart.println("-> START Data Stream");
    Serial.println("收到指令: S (開始發送)");
  } else if (cmd == "P") {
    isSendingData = false;
    bleuart.println("-> PAUSE Data Stream");
    Serial.println("收到指令: P (暫停發送)");
  } else if (cmd == "C") {
    Serial.println("收到指令: C (自查 IIC)");
    bleuart.println("-> Checking IIC Bus...");
    uint8_t error, address;
    int nDevices = 0;
    bool foundBMP581 = false;
    for(address = 1; address < 127; address++ ) {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      if (error == 0) {
        String msg = "-> IIC device found at 0x";
        if (address < 16) msg += "0";
        msg += String(address, HEX);
        bleuart.println(msg);
        if (address == BMP581_ADDR) {
          foundBMP581 = true;
        }
        nDevices++;
      } else if (error == 4) {
        String msg = "-> Unknown error at 0x";
        if (address < 16) msg += "0";
        msg += String(address, HEX);
        bleuart.println(msg);
      }
    }
    if (nDevices == 0) {
      bleuart.println("-> MBR: Error! No IIC devices found.");
    } else {
      if (foundBMP581) {
         bleuart.println("-> BMP581 Status: OK (0x47 Connected)");
      } else {
         bleuart.println("-> BMP581 Status: Error! (0x47 NOT found)");
      }
    }
  }
}