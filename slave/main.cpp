#include <Arduino.h>
#include <DHT.h>
#include <HardwareSerial.h>
#include <PZEM004Tv30.h>

// ============================================================================
// 1. CẤU HÌNH CHÂN GPIO (đặt hết ở đầu file để dễ chỉnh sửa)
// ============================================================================

// --- Cảm biến DHT11 (nhiệt độ, độ ẩm) ---
#define DHT_PIN 25
#define DHT_TYPE DHT11

// --- Cảm biến khí gas MQ-2 / MQ135 ---
#define MQ_PIN 32
// true = đọc kiểu Analog (AO) - theo đúng sơ đồ nguyên lý đã thi công (mục 3.1.3.c)
// false = đọc kiểu Digital (DO). Đổi lại nếu board của bạn nối chân DO.
const bool MQ_USE_ANALOG = true;

// --- Cảm biến ánh sáng LM393 / LDR (quang trở) ---
#define LDR_PIN 34
// true = đọc kiểu Analog (AO) - theo đúng sơ đồ nguyên lý đã thi công (mục 3.1.3.c)
const bool LDR_USE_ANALOG = true;

// --- Relay 2 kênh ---
#define RELAY1_PIN 27   // Relay 1: điều khiển tải / ổ cắm
#define RELAY2_PIN 26   // Relay 2: điều khiển đèn
// Mức tín hiệu để relay ĐÓNG (kích hoạt). Mặc định giả định module relay
// kích mức LOW (phổ biến ở các module relay 5V thông dụng).
// ĐỔI thành HIGH nếu module relay của bạn kích mức HIGH.
#define RELAY_ACTIVE_LEVEL LOW

// --- Buzzer (qua transistor C1815) ---
#define BUZZER_PIN 13

// --- LED báo trạng thái ---
#define LED_STATUS_PIN 14

// --- PZEM-004T (UART1) ---
// CẦN KIỂM TRA LẠI THEO SƠ ĐỒ NGUYÊN LÝ (xem ghi chú ở đầu file)
#define PZEM_RX_PIN 21   // Chân RX của ESP32 - nối với TX của PZEM
#define PZEM_TX_PIN 22   // Chân TX của ESP32 - nối với RX của PZEM

// --- LoRa E32-433T20D (UART2) ---
#define LORA_RX_PIN 16   // Chân RX của ESP32 - nối với TX của LoRa
#define LORA_TX_PIN 17   // Chân TX của ESP32 - nối với RX của LoRa
#define LORA_M0_PIN 4
#define LORA_M1_PIN 0    // GPIO0 là chân BOOT - lưu ý khi nạp code
#define LORA_AUX_PIN 15  // GPIO15 liên quan mức boot - CẦN KIỂM TRA LẠI THEO SƠ ĐỒ NGUYÊN LÝ

// Các chân mở rộng (chưa dùng): GPIO35, GPIO33, GPIO2 (GPIO2 là chân boot)

// ============================================================================
// 2. NGƯỠNG CẢNH BÁO - CẦN HIỆU CHỈNH THEO THỰC TẾ
// ============================================================================
int   GAS_THRESHOLD     = 250;    // Ngưỡng khí gas (giá trị ADC thô 0-4095), theo thực nghiệm báo cáo mục 4.2.4
int   LIGHT_THRESHOLD   = 2000;   // Ngưỡng ánh sáng (ADC 0-4095) để bật đèn tự động - GIÁ TRỊ MẪU, CẦN HIỆU CHỈNH
float POWER_THRESHOLD   = 500.0;  // Ngưỡng công suất quá tải (W) - GIÁ TRỊ MẪU, CẦN HIỆU CHỈNH
float CURRENT_THRESHOLD = 2.0;    // Ngưỡng dòng điện quá tải (A) - GIÁ TRỊ MẪU, CẦN HIỆU CHỈNH

// Chu kỳ thời gian (không dùng delay dài, dùng millis())
const unsigned long SENSOR_READ_INTERVAL = 2000; // đọc cảm biến mỗi 2 giây
const unsigned long SEND_INTERVAL        = 2000; // gửi dữ liệu về Master mỗi 2 giây

// ============================================================================
// 3. ĐỐI TƯỢNG TOÀN CỤC
// ============================================================================
DHT dht(DHT_PIN, DHT_TYPE);
HardwareSerial loraSerial(2);                                 // UART2 cho LoRa
HardwareSerial pzemSerial(1);                                 // UART1 cho PZEM
PZEM004Tv30 pzem(pzemSerial, PZEM_RX_PIN, PZEM_TX_PIN);        // Thư viện tự begin Serial1 với 2 chân này

// --- Biến trạng thái / dữ liệu ---
float temperature = 0, humidity = 0;
int   gasValue = 0, lightValue = 0;
float voltage = 0, current = 0, power = 0, energy = 0;
bool  relay1State = false;
bool  relay2State = false;
String mode = "AUTO";          // AUTO hoặc MANUAL
String alarmState = "NORMAL";  // NORMAL, GAS_ALARM, OVERLOAD, SENSOR_ERROR
bool  sensorError = false;
bool  unsafeCondition = false; // true khi đang trong tình trạng nguy hiểm (gas/quá tải)

String rxBuffer = "";
unsigned long lastSensorRead = 0;
unsigned long lastSendTime = 0;
unsigned long lastLedBlink = 0;
bool ledState = false;

// ============================================================================
// 4. KHAI BÁO NGUYÊN MẪU HÀM (bắt buộc vì dùng file .cpp, không phải .ino)
// ============================================================================
void setupPins();
void setupLoRa();
void setupPZEM();
void readSensors();
void readPZEM();
int  readGasSensor();
int  readLightSensor();
void handleAutoMode();
void checkAlarm();
void handleLoRaCommand();
void processCommand(String cmd);
void setRelay1(bool state);
void setRelay2(bool state);
String buildDataPacket();
void sendDataToMaster(String packet);
void sendDataPacket();

// ============================================================================
// 5. SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== NODE SLAVE - DANG KHOI DONG ===");

  setupPins();
  setupLoRa();
  setupPZEM();
  dht.begin();

  // Trạng thái an toàn khi khởi động: Relay OFF, Buzzer OFF
  setRelay1(false);
  setRelay2(false);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("=== NODE SLAVE SAN SANG HOAT DONG ===");
}

// ============================================================================
// 6. LOOP CHÍNH - không dùng delay dài, dùng millis()
// ============================================================================
void loop() {
  unsigned long now = millis();

  // --- Nhận lệnh điều khiển từ Master (đọc liên tục, không chặn) ---
  handleLoRaCommand();

  // --- Đọc cảm biến + xử lý logic định kỳ ---
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    readSensors();
    readPZEM();
    checkAlarm();
    if (mode == "AUTO") {
      handleAutoMode();
    }
  }

  // --- Gửi dữ liệu về Master định kỳ ---
  if (now - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = now;
    sendDataPacket();
  }

  // --- Nhấp nháy LED trạng thái (heartbeat), không chặn chương trình ---
  if (now - lastLedBlink >= 1000) {
    lastLedBlink = now;
    ledState = !ledState;
    digitalWrite(LED_STATUS_PIN, ledState);
  }
}

// ============================================================================
// 7. CÀI ĐẶT CHÂN GPIO
// ============================================================================
void setupPins() {
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_STATUS_PIN, OUTPUT);

  // GPIO34 chỉ input-only, không cần pinMode khi dùng analogRead()
  if (!MQ_USE_ANALOG)  pinMode(MQ_PIN, INPUT);
  if (!LDR_USE_ANALOG) pinMode(LDR_PIN, INPUT);

  digitalWrite(RELAY1_PIN, !RELAY_ACTIVE_LEVEL); // OFF
  digitalWrite(RELAY2_PIN, !RELAY_ACTIVE_LEVEL); // OFF
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_STATUS_PIN, LOW);

  Serial.println("[OK] Da cau hinh xong cac chan GPIO");
}

// ============================================================================
// 8. KHỞI TẠO LORA - đưa module vào Normal Mode (M0=LOW, M1=LOW)
// ============================================================================
void setupLoRa() {
  pinMode(LORA_M0_PIN, OUTPUT);
  pinMode(LORA_M1_PIN, OUTPUT);
  pinMode(LORA_AUX_PIN, INPUT);

  digitalWrite(LORA_M0_PIN, LOW);
  digitalWrite(LORA_M1_PIN, LOW); // Normal Mode: mở cổng Serial truyền/nhận

  loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  delay(100); // chờ module ổn định sau khi chuyển mode (chỉ chạy 1 lần lúc khởi động)

  Serial.println("[OK] LoRa E32 da vao Normal Mode (M0=LOW, M1=LOW)");
}

// ============================================================================
// 9. KHỞI TẠO PZEM-004T
// ============================================================================
void setupPZEM() {
  // Thư viện PZEM004Tv30 tự động gọi Serial1.begin(9600,...) với 2 chân đã khai báo
  Serial.println("[OK] PZEM-004T da khoi tao tren UART1");
}

// ============================================================================
// 10. ĐỌC CẢM BIẾN MÔI TRƯỜNG
// ============================================================================
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    sensorError = true;
    Serial.println("[LOI] Khong doc duoc DHT11! Giu nguyen gia tri cu.");
    // Không cập nhật temperature/humidity để tránh gửi dữ liệu rác (0/NaN) về Master
  } else {
    temperature = t;
    humidity = h;
    sensorError = false;
  }

  gasValue = readGasSensor();
  lightValue = readLightSensor();
}

// Đọc cảm biến khí gas - linh hoạt Analog/Digital
int readGasSensor() {
  if (MQ_USE_ANALOG) {
    return analogRead(MQ_PIN); // 0 - 4095 (ESP32 ADC 12-bit)
  } else {
    // Quy đổi tín hiệu số (DO) về thang tương đương để logic ngưỡng dùng chung 1 công thức
    return digitalRead(MQ_PIN) == HIGH ? 4095 : 0;
  }
}

// Đọc cảm biến ánh sáng - linh hoạt Analog/Digital
int readLightSensor() {
  if (LDR_USE_ANALOG) {
    return analogRead(LDR_PIN);
  } else {
    return digitalRead(LDR_PIN) == HIGH ? 4095 : 0;
  }
}

// ============================================================================
// 11. ĐỌC PZEM-004T (điện áp, dòng điện, công suất, điện năng)
// ============================================================================
void readPZEM() {
  float v = pzem.voltage();
  float i = pzem.current();
  float p = pzem.power();
  float e = pzem.energy();

  if (isnan(v)) {
    // PZEM trả về NaN khi mất kết nối / lỗi đọc Modbus
    Serial.println("[LOI] Khong doc duoc PZEM-004T! Kiem tra day noi UART va nguon 220V.");
    sensorError = true;
    // Giữ nguyên các giá trị cũ, không ghi đè bằng 0 để tránh hiển thị sai trên Web
  } else {
    voltage = v;
    current = isnan(i) ? 0 : i;
    power   = isnan(p) ? 0 : p;
    energy  = isnan(e) ? 0 : e;
  }
}

// ============================================================================
// 12. LOGIC CHẾ ĐỘ AUTO - tự động bật/tắt đèn theo cường độ ánh sáng
// ============================================================================
void handleAutoMode() {
  // CẦN KIỂM TRA LẠI THEO SƠ ĐỒ NGUYÊN LÝ: giả định quang trở LDR mắc kiểu
  // ADC càng CAO nghĩa là môi trường càng SÁNG (tùy cách phân áp trên module
  // LM393 mà chiều này có thể ngược lại - nếu đèn hoạt động ngược, hãy đảo
  // dấu so sánh bên dưới).
  if (lightValue < LIGHT_THRESHOLD) {
    setRelay2(true);  // trời tối -> bật đèn
  } else {
    setRelay2(false); // đủ sáng -> tắt đèn
  }
}

// ============================================================================
// 13. LOGIC CẢNH BÁO AN TOÀN - ưu tiên cao nhất, chạy độc lập mỗi chu kỳ
// ============================================================================
void checkAlarm() {
  unsafeCondition = false;

  if (sensorError) {
    // Lỗi cảm biến: chỉ báo lỗi, KHÔNG ngắt relay (không phải tình huống nguy hiểm vật lý)
    if (alarmState == "NORMAL") alarmState = "SENSOR_ERROR";
    return;
  }

  if (gasValue > GAS_THRESHOLD) {
    unsafeCondition = true;
    alarmState = "GAS_ALARM";
  } else if (power > POWER_THRESHOLD || current > CURRENT_THRESHOLD) {
    unsafeCondition = true;
    alarmState = "OVERLOAD";
  }

  if (unsafeCondition) {
    // Nguy hiểm: bật còi, ngắt toàn bộ relay để bảo vệ tải
    digitalWrite(BUZZER_PIN, HIGH);
    setRelay1(false);
    setRelay2(false);
  } else {
    // An toàn: tắt còi ngay (theo lưu đồ hình 3.9), nhưng KHÔNG tự xóa
    // alarmState - trạng thái cảnh báo trên Web chỉ được xóa khi có lệnh
    // RESET_ALARM, để người dùng biết đã từng xảy ra sự cố.
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ============================================================================
// 14. NHẬN VÀ XỬ LÝ LỆNH TỪ MASTER QUA LORA
// ============================================================================
void handleLoRaCommand() {
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      processCommand(rxBuffer);
      rxBuffer = "";
    } else if (c != '\r') {
      rxBuffer += c;
      if (rxBuffer.length() > 64) rxBuffer = ""; // chống tràn bộ đệm nếu nhiễu sóng
    }
  }
}

void processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "SET_AUTO") {
    mode = "AUTO";
    Serial.println("-> Chuyen sang che do AUTO");
  } else if (cmd == "SET_MANUAL") {
    mode = "MANUAL";
    Serial.println("-> Chuyen sang che do MANUAL");
  } else if (cmd == "RELAY1_ON") {
    if (mode == "MANUAL") setRelay1(true);
  } else if (cmd == "RELAY1_OFF") {
    if (mode == "MANUAL") setRelay1(false);
  } else if (cmd == "RELAY2_ON") {
    if (mode == "MANUAL") setRelay2(true);
  } else if (cmd == "RELAY2_OFF") {
    if (mode == "MANUAL") setRelay2(false);
  } else if (cmd == "BUZZER_OFF") {
    if (!unsafeCondition) digitalWrite(BUZZER_PIN, LOW);
  } else if (cmd == "RESET_ALARM") {
    if (!unsafeCondition) {
      alarmState = "NORMAL";
      Serial.println("-> Da xoa trang thai canh bao");
    } else {
      Serial.println("-> Khong the xoa canh bao: dieu kien van con nguy hiem");
    }
  } else {
    Serial.println("[CANH BAO] Lenh khong hop le, bo qua: " + cmd);
  }
}

// ============================================================================
// 15. ĐIỀU KHIỂN RELAY (áp dụng RELAY_ACTIVE_LEVEL)
// ============================================================================
void setRelay1(bool state) {
  relay1State = state;
  digitalWrite(RELAY1_PIN, state ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
}

void setRelay2(bool state) {
  relay2State = state;
  digitalWrite(RELAY2_PIN, state ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
}

// ============================================================================
// 16. ĐÓNG GÓI VÀ GỬI DỮ LIỆU VỀ MASTER
// ============================================================================
String buildDataPacket() {
  String packet = "SLAVE01,";
  packet += String(temperature, 1) + ",";
  packet += String(humidity, 1) + ",";
  packet += String(gasValue) + ",";
  packet += String(lightValue) + ",";
  packet += String(voltage, 1) + ",";
  packet += String(current, 2) + ",";
  packet += String(power, 1) + ",";
  packet += String(energy, 3) + ",";
  packet += String(relay1State ? 1 : 0) + ",";
  packet += String(relay2State ? 1 : 0) + ",";
  packet += mode + ",";
  packet += alarmState;
  packet += "\n";
  return packet;
}

void sendDataToMaster(String packet) {
  loraSerial.print(packet);
}

void sendDataPacket() {
  String packet = buildDataPacket();
  sendDataToMaster(packet);
  Serial.print("[GUI] " + packet);
}
