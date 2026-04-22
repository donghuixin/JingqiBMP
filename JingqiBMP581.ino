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
unsigned long lastSampleTime = 0;
const int SAMPLE_INTERVAL_MS = 10; // 提高采樣率至 10 毫秒 (100Hz)
float pressureBuffer[10]; // 建立一個緩衝區儲存 10 次的采樣
int sampleIndex = 0;
bool triggerInit = false; // 用於在主迴圈中觸發初始化，避免 Callback 卡死
bool triggerIICCheck = false; // 用於在主迴圈中觸發 IIC 掃描，避免 Callback 卡死

// ================= 初始化副程式 =================
void initSensor() {
  unsigned long startT = millis();
  Serial.println("開始初始化 BMP581...");
  if (Bluefruit.connected()) {
    bleuart.println("-> 開始初始化 BMP581...");
  }
  
  if (bmp58x.begin()) {
    isBmpReady = true;
    bmp58x.setMeasureMode(bmp58x.eNormal);
    unsigned long duration = millis() - startT;
    
    Serial.print("BMP581 初始化成功 (0x47), 耗時: ");
    Serial.print(duration);
    Serial.println(" ms");
    
    if (Bluefruit.connected()) {
      bleuart.print("-> BMP581 初始化成功 (0x47), 耗時: ");
      bleuart.print(duration);
      bleuart.println(" ms");
      delay(50);
      
      // 避免 MTU (20 Bytes) 限制，拆分成多個短指令發送
      bleuart.println("INIT:OK");
      delay(50); // 稍微延遲避免藍牙緩衝區塞車
      bleuart.println("SR:100Hz(Batch)");
      delay(50);
      bleuart.println("RNG:30~125kPa");
    }
  } else {
    isBmpReady = false;
    Serial.println("BMP581 初始化失敗！請檢查硬體。");
    if (Bluefruit.connected()) {
      bleuart.println("-> BMP581 初始化失敗！請檢查硬體。");
      delay(50);
      bleuart.println("INIT:FAIL");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000); // 讓系統啟動穩定

  Serial.println("System Starting...");

  // 1. 初始化 I2C 與 BMP581 (開啟 D4, D5 上拉)
  pinMode(D4, INPUT_PULLUP);
  pinMode(D5, INPUT_PULLUP);
  Wire.begin();

  initSensor();

  // 2. 初始化 Bluefruit 藍牙
  Bluefruit.begin();
  
  // 原先設定的 configPrphBandwidth(BANDWIDTH_MAX) 可能與某些平臺不相容導致剛連線就斷開。
  // 我們退回較保守但依然快速的設定
  Bluefruit.Periph.setConnInterval(12, 40); // 15ms ~ 50ms 
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
  // 將所有長時延遲和占用 IIC 通訊的工作統一放在主迴圈 (Main Task) 執行，避免在 BLE 內部 Callback 當中因為阻塞導致藍牙當機斷線
  if (triggerInit && Bluefruit.connected()) {
    triggerInit = false;
    delay(500); // 稍微等待，確保通訊通道建立完成
    initSensor();
    if (isBmpReady) {
      bleuart.println("Send 'S' to start, 'P' to pause, 'C' to check.");
    }
  }

  // 在主迴圈中執行耗時的 IIC 總線掃描
  if (triggerIICCheck && Bluefruit.connected()) {
    triggerIICCheck = false;
    
    bleuart.println("-> Checking IIC Bus...");
    uint8_t error, address;
    int nDevices = 0;
    bool foundBMP581 = false;
    
    // IIC 掃描會阻塞很長一段時間，因此必須在 loop 中執行
    for(address = 1; address < 127; address++ ) {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      
      if (error == 0) {
        String msg = "-> IIC device found at 0x";
        if (address < 16) msg += "0";
        msg += String(address, HEX);
        if (bleuart.availableForWrite() >= msg.length()) { // 防呆
            bleuart.println(msg);
        }
        if (address == BMP581_ADDR) {
          foundBMP581 = true;
        }
        nDevices++;
      } else if (error == 4) {
        String msg = "-> Unknown error at 0x";
        if (address < 16) msg += "0";
        msg += String(address, HEX);
        if (bleuart.availableForWrite() >= msg.length()) { // 防呆
            bleuart.println(msg);
        }
      }
      delay(5); // 給藍牙堆疊喘息時間
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

  // 主迴圈只要專心處理「定時量測與發送資料」即可
  if (isSendingData && isBmpReady && Bluefruit.connected()) {
    if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
      lastSampleTime = millis();
      
      float pressure = bmp58x.readPressPa();
      pressureBuffer[sampleIndex] = pressure;
      sampleIndex++;
      
      // 每收集滿 10 筆數據 (約 100ms) 進行一次藍牙打包發送
      if (sampleIndex >= 10) {
        // 利用最後一筆資料當作健康狀態判斷
        String statusVal = (pressureBuffer[9] > 0) ? "1" : "0";
        
        // 格式化為: B:val1,val2,...,val10|S:1\n
        String dataMsg = "B:";
        for (int i = 0; i < 10; i++) {
          dataMsg += String(pressureBuffer[i], 2);
          if (i < 9) dataMsg += ",";
        }
        dataMsg += "|S:" + statusVal + "\n";
        
        // 直接發送打包後的長字串，底層藍牙協議棧會自動進行分包
        bleuart.print(dataMsg);
        
        Serial.print("藍牙批次發送: ");
        Serial.print(dataMsg);
        
        sampleIndex = 0; // 重置緩衝區指針
      }
    }
  }
  
  // 給 Bluefruit 作業系統處理背景藍牙事件的空間
  delay(1);
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

  // 將連線訊息也同步發送給上位機終端
  bleuart.print("-> 已連線至設備: ");
  bleuart.println(central_name);

  // 利用標誌位，通知主迴圈去執行耗時的元件初始化任務
  triggerInit = true;
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
    // 不能在藍牙 Rx Callback 中直接執行耗時的 Wire 掃描
    triggerIICCheck = true; 
  }
}