#include <WiFi.h>
#include <Wire.h>

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESP32Servo.h>  // Thay cho #include <Servo.h>
#include <EEPROM.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define EEPROM_SIZE 512
#define PASS_ADDR 0  // Vị trí lưu mật khẩu trong EEPROM
//0 là có cháy 1 là ko

// ================= PIN DEFINE =================
#define LIVING_ROOM_LIGHT 12
#define BED_ROOM_LIGHT 13
#define PUMP 14
#define FAN 27
#define SIREN 25
#define DOOR_SERVO 26

#define RAIN_SENSOR 34   // ADC1
#define FLAME_SENSOR 35  // ADC1
#define GAS_SENSOR 32    // ADC1
#define DHTPIN 15
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ================= GLOBAL =================
Servo door;
bool alarmActive = false;
bool statuscheckpass = false;
unsigned long lastRequest = 0;
unsigned long lastTempTime = 0;
bool prevFlame = false;
bool prevDanger = false;
bool isFlameSensorActive = true;
bool isGasSensorActive = true;
bool isRainSensorActive = true;

String inputPass = "";

String passwordFromServer = "";
unsigned long lastPasswordChange = 0;

// Biến lưu mức sensor
int fs_level = 0;  // flame
int rs_level = 0;  // rain
int gs_level = 0;  // ga
unsigned long lastSendTime = 0;
const unsigned long interval1Min = 60000;  // 1 phút
bool prevGas = false;
bool systemIsDanger = false;   // trạng thái hiện tại
bool lastDangerState = false;  // trạng thái trước đó
// WiFi
const char* ssid = "Quan Quyen Luong";
const char* password = "1593572486";

// API URL
String server = "http://192.168.1.122:5435";

const byte ROWS = 4;  // 4 hàng
const byte COLS = 4;  // 4 cột

// Khai báo mảng ký tự phím
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};


// Keypad 4x4 - KHÔNG TRÙNG CHÂN NÀO

byte rowPins[ROWS] = { 18, 19, 23, 33 };
byte colPins[COLS] = { 4, 5, 16, 17 };
// Tạo đối tượng keypad
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
// ================= FUNCTIONS =================
void savePasswordToEEPROM(String pwd) {
  EEPROM.writeString(PASS_ADDR, pwd);
  EEPROM.commit();
  Serial.println("Đã lưu mật khẩu vào EEPROM!");
}
// ============================================================
//   HÀM ĐỌC MẬT KHẨU TỪ EEPROM
// ============================================================
String loadPasswordFromEEPROM() {
  String pwd = EEPROM.readString(PASS_ADDR);
  return pwd;
}
void getPasswordFromAPI() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(server + "/home/password");
    http.addHeader("x-api-key", "esp32smarthousekey");

    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("Response: " + payload);

      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        passwordFromServer = doc["password"].as<String>();
        Serial.println("Mật khẩu từ server: " + passwordFromServer);

        // Lưu vào EEPROM
        savePasswordToEEPROM(passwordFromServer);

      } else {
        Serial.println("Lỗi parse JSON!");
      }
    } else {
      Serial.println("Lỗi GET API!");
    }

    http.end();
  }
}
void fetchStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("GET all-status: out ");
    return;
  }

  HTTPClient http;

  http.begin(server + "/home/all-status");
    http.addHeader("x-api-key", "esp32smarthousekey");

  int code = http.GET();
  Serial.println("GET all-status: " + code);

  if (code == 200) {
    String payload = http.getString();
    Serial.println("GET all-status: " + payload);

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      digitalWrite(PUMP, doc["pump"] ? LOW : HIGH);
      digitalWrite(FAN, doc["fan"] ? LOW : HIGH);
      digitalWrite(LIVING_ROOM_LIGHT, doc["living_led"] ? HIGH : LOW);
      digitalWrite(BED_ROOM_LIGHT, doc["bed_led"] ? HIGH : LOW);
      digitalWrite(SIREN, doc["buzzer"] ? HIGH : LOW);
      isFlameSensorActive = doc["fs"];
      isGasSensorActive = doc["gs"];
      isRainSensorActive = doc["rs"];
      Serial.println(
        String("GET sensor: FS=") + (isFlameSensorActive ? "true" : "false") + " GS=" + (isGasSensorActive ? "true" : "false") + " RS=" + (isRainSensorActive ? "true" : "false"));

      // Door servo
      if (doc["door"]) door.write(170);  // mở
      else door.write(90);               // đóng
    }
  }
  http.end();
}

void postEmergency(String type) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(server + "/esp/update");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", "esp32smarthousekey");

  String json = "{\"value\":\"" + type + "\"}";
  int code = http.POST(json);

  Serial.print("POST ");
  Serial.print(type);
  Serial.print(" => ");
  Serial.println(code);

  http.end();
}

void triggerAlarm(String type) {
  digitalWrite(SIREN, HIGH);
  door.write(90);
  postEmergency(type);  // chỉ khi sensor chuyển LOW->HIGH
}

void sendTempHumid() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int rainLevel = analogRead(RAIN_SENSOR);    // ADC 32
  bool isRain = (rainLevel < rs_level);

  if (isnan(temperature) || isnan(humidity)) return;

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(server + "/home/temp-humid");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", "esp32smarthousekey");

    String json = "{";
    json += "\"temperature\":" + String(temperature) + ",";
    json += "\"humidity\":" + String(humidity) + ",";
    json += "\"rain\":";
    json += (isRain ? "true" : "false");
    json += "}";

    int code = http.POST(json);
Serial.println(
    "POST temp-humid => " + String(code) +
    " | rain=" + (isRain ? "true" : "false")
);

    http.end();
  }
}

void getSensorLevels() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(server + "/home/all-levels");
    http.addHeader("x-api-key", "esp32smarthousekey");
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();

    // JSON object
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      fs_level = doc["fs_level"];
      rs_level = doc["rs_level"];
      gs_level = doc["gs_level"];
    }
  }

  http.end();
}
void getKey() {
  char key = keypad.getKey();  // Đọc phím

  if (key) {

    // ===================== NHẬP SỐ ======================
    if (key >= '0' && key <= '9') {
      statuscheckpass = true;
      inputPass += key;  // ghi chuỗi
      Serial.print("Mã nhập: ");
      Serial.println(inputPass);

      // Hiển thị trực tiếp trên LCD
      lcd.setCursor(0, 1);
      lcd.print("                ");  // xóa dòng trước
      lcd.setCursor(0, 1);
      lcd.print(inputPass);  // in trực tiếp số nhập
    }

    // ===================== XÓA TẤT CẢ =====================
    else if (key == '*') {
      inputPass = "";
      Serial.println("Đã xóa toàn bộ.");

      lcd.setCursor(0, 1);
      lcd.print("                ");
    }

    // ===================== XÓA SỐ CUỐI ====================
    else if (key == 'A') {
      if (inputPass.length() > 0) {
        inputPass.remove(inputPass.length() - 1);
      }

      Serial.print("Xóa số cuối: ");
      Serial.println(inputPass);

      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(inputPass);
    }

    // ===================== GỬI PASS =======================
    else if (key == 'B') {
      Serial.println("Gửi mật khẩu...");

      checkPass();  // kiểm tra

      inputPass = "";  // reset sau khi gửi
      lcd.setCursor(0, 0);
      lcd.print("Xin chao");
      lcd.setCursor(0, 1);
      lcd.print("                ");
    }
  }
}


void checkPass() {
  if (inputPass == passwordFromServer) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("mat khau dung.");
    delay(1000);
    lcd.clear();
    statuscheckpass = false;

    Serial.println("Mật khẩu đúng! Mở khóa!");
    sendDoorStatusToServer(true);

    door.write(90);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("mat khau sai.");
    Serial.println("Mật khẩu sai! Vui lòng thử lại.");
    delay(1000);
    statuscheckpass = false;
  }
  inputPass = "";  // Reset lại mã nhập
}
void sendDoorStatusToServer(bool open) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(server + "/home/update-door");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", "esp32smarthousekey");

    String json = "{\"status\": " + String(open ? "true" : "false") + "}";

    int httpCode = http.POST(json);

    if (httpCode > 0) {
      Serial.println("Server response: " + http.getString());
    } else {
      Serial.println("Failed to send door status");
    }

    http.end();
  } else {
    Serial.println("WiFi disconnected, can't send door status!");
  }
}

void sendLevel(String endpoint, int level) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(server + "/home/" + endpoint);
  http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", "esp32smarthousekey");

  String json = "{\"level\":" + String(level) + "}";

  int code = http.POST(json);
  Serial.print("Send ");
  Serial.print(endpoint);
  Serial.print(" => ");
  Serial.println(code);

  http.end();
}
void sendHello() {
  HTTPClient http;
  http.begin(server + "/home/hello");
  http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", "esp32smarthousekey");

  int code = http.GET();
  Serial.println("Hello sent, code = " + String(code));

  http.end();
}

void senTempAndHumidChart() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int rain = digitalRead(RAIN_SENSOR);
  bool isRain = (rain == LOW);

  if (isnan(temperature) || isnan(humidity)) return;

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(server + "/home/temp-humid/chart");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key","esp32smarthousekey");

    String json = "{";
    json += "\"temperature\":" + String(temperature) + ",";
    json += "\"humidity\":" + String(humidity) + ",";
    json += "\"rain\":";
    json += (isRain ? "true" : "false");
    json += "}";

    int code = http.POST(json);
    Serial.println("POST temp-humid/chart => " + String(code));

    http.end();
  }
}
// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  getSensorLevels();
  Wire.begin(21, 22);  // SDA = 21, SCL = 22
  dht.begin();         // Khởi tạo DHT
  EEPROM.begin(EEPROM_SIZE);
  passwordFromServer = loadPasswordFromEEPROM();
  getPasswordFromAPI();
  lcd.init();           // khởi tạo LCD
  lcd.backlight();      // bật đèn nền
  lcd.setCursor(0, 0);  // cột 0, hàng 0
  lcd.print("Xin chao!");
  // ----- ĐỌC LẠI EEPROM -----
  Serial.print("Mật khẩu lưu trong EEPROM: ");
  Serial.println(passwordFromServer);
  pinMode(LIVING_ROOM_LIGHT, OUTPUT);
  pinMode(BED_ROOM_LIGHT, OUTPUT);
  pinMode(PUMP, OUTPUT);
  pinMode(FAN, OUTPUT);
  pinMode(SIREN, OUTPUT);
  pinMode(RAIN_SENSOR, INPUT);

  pinMode(FLAME_SENSOR, INPUT);
  pinMode(GAS_SENSOR, INPUT);
  door.attach(DOOR_SERVO);
  door.write(90);

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  int flameLevel = analogRead(FLAME_SENSOR);  // ADC 35
  int gasLevel = analogRead(GAS_SENSOR);      // ADC 32
  int rainLevel = analogRead(RAIN_SENSOR);    // ADC 32
  sendHello();
  sendLevel("update-fs/data", flameLevel);
  sendLevel("update-gs/data", gasLevel);
  sendLevel("update-rs/data", rainLevel);
  senTempAndHumidChart();
}
// ================= LOOP =================
void loop() {
  // ----------- ĐỌC CẢM BIẾN -----------

  char key = keypad.getKey();  // Đọc phím
  if (key) {
    statuscheckpass = true;
  }
  if (statuscheckpass == false) {
    fetchStatus();
    getSensorLevels();
    if (millis() - lastPasswordChange > 2000) {
      getPasswordFromAPI();
      // ----- ĐỌC LẠI EEPROM -----
      String pwd = loadPasswordFromEEPROM();
      Serial.print("Mật khẩu lưu trong EEPROM: ");
      Serial.println(pwd);
      lastPasswordChange = millis();
      sendTempHumid();
    }
    // Đọc giá trị analog từ cảm biến
    int flameLevel = analogRead(FLAME_SENSOR);  // ADC 35
    int gasLevel = analogRead(GAS_SENSOR);      // ADC 32
    int rainLevel = analogRead(RAIN_SENSOR);    // ADC 32



    unsigned long current = millis();
    if (current - lastSendTime >= interval1Min) {
      lastSendTime = current;
      Serial.print("gửi data ");
      senTempAndHumidChart();
      sendLevel("update-fs/data", flameLevel);
      sendLevel("update-gs/data", gasLevel);
      sendLevel("update-rs/data", rainLevel);
    }


    Serial.print("Flame Level: ");
    Serial.println(flameLevel);

     Serial.print("Rain Level: ");
    Serial.println(rainLevel);

    Serial.print("Gas Level: ");
    Serial.println(gasLevel);


    if (isFlameSensorActive && flameLevel < fs_level) {  // tình huống có cháy
      digitalWrite(PUMP, HIGH);
      digitalWrite(FAN, HIGH);
      digitalWrite(SIREN, HIGH);
      door.write(170);
      prevDanger = true;
      Serial.println("có cháy");
      if (alarmActive == false) {
        postEmergency("cháy");
        alarmActive = true;
      }
    } else if (isFlameSensorActive && gasLevel > gs_level) {
      digitalWrite(PUMP, HIGH);
      digitalWrite(FAN, HIGH);
      digitalWrite(SIREN, HIGH);
      prevDanger = true;
      door.write(170);
      Serial.println("có khói");

      if (alarmActive == false) {
        postEmergency("khói");
        alarmActive = true;
      }
    } else if (flameLevel >= fs_level && gasLevel <= gs_level) {
      Serial.println("bình thường");

      if (prevDanger) {
        postEmergency("bình thường");
      }
      prevDanger = false;
      alarmActive = false;
    }
  } else {
    getKey();
  }
}