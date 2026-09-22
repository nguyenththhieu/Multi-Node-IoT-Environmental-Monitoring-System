#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>

// ============================================================================
// 1. CẤU HÌNH WIFI - SỬA LẠI CHO ĐÚNG MẠNG CỦA BẠN
// ============================================================================
const char* WIFI_SSID     = "TEN_WIFI_CUA_BAN";
const char* WIFI_PASSWORD = "MAT_KHAU_WIFI";

// ============================================================================
// 2. TÀI KHOẢN ĐĂNG NHẬP WEB (đồ án sinh viên - đăng nhập cơ bản)
// ============================================================================
const char* LOGIN_USER = "admin";
const char* LOGIN_PASS = "123456";
bool isLoggedIn = false; // Đơn giản hoá: 1 biến toàn cục dùng chung cho phiên đăng nhập

// ============================================================================
// 3. CẤU HÌNH CHÂN GPIO
// ============================================================================
#define LED_WIFI_PIN 14   // Sáng khi WiFi đã kết nối
#define LED_LORA_PIN 25   // Nhấp nháy khi nhận được gói tin LoRa

// LoRa E32-433T20D - UART2
#define LORA_RX_PIN 16
#define LORA_TX_PIN 17
#define LORA_M0_PIN 4
#define LORA_M1_PIN 0     // GPIO0 la chan BOOT - luu y khi nap code
#define LORA_AUX_PIN 15   // CẦN KIỂM TRA LẠI THEO SƠ ĐỒ NGUYÊN LÝ

// ============================================================================
// 4. THAM SỐ HỆ THỐNG
// ============================================================================
const unsigned long LORA_TIMEOUT_MS = 10000; // quá 10s không nhận được dữ liệu -> mất kết nối

// ============================================================================
// 5. ĐỐI TƯỢNG TOÀN CỤC
// ============================================================================
WebServer server(80);
HardwareSerial loraSerial(2);

// --- Dữ liệu mới nhất nhận từ Slave ---
String slaveID = "--";
float g_temp = 0, g_hum = 0;
int   g_gas = 0, g_light = 0;
float g_voltage = 0, g_current = 0, g_power = 0, g_energy = 0;
int   g_relay1 = 0, g_relay2 = 0;
String g_mode = "AUTO";
String g_alarm = "NORMAL";

unsigned long lastLoRaReceived = 0;
bool connectionOK = false;

String rxBuffer = "";
unsigned long loraLedOnTime = 0;

// ============================================================================
// 6. KHAI BÁO NGUYÊN MẪU HÀM
// ============================================================================
void setupLoRa();
void connectWiFi();
void setupWebServer();
void readLoRaData();
void parseDataPacket(String packet);
void checkConnectionTimeout();
void sendCommandToSlave(String cmd);

bool requireLogin();
bool requireLoginJson();

void handleRoot();
void handleLoginPage();
void handleLoginSubmit();
void handleLogout();
void handleHome();
void handleOverview();
void handlePower();
void handleControl();
void handleApiData();
void handleCmd();
void handleStyleCss();
void handleAppJs();

String getNavbar(String active);
String getPageStart(String title, String active);
String getPageEnd();
String getLoginPageHTML(String errorMsg);
String getStyleCSS();
String getAppJS();

// ============================================================================
// 7. SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== NODE MASTER - DANG KHOI DONG ===");

  pinMode(LED_WIFI_PIN, OUTPUT);
  pinMode(LED_LORA_PIN, OUTPUT);
  digitalWrite(LED_WIFI_PIN, LOW);
  digitalWrite(LED_LORA_PIN, LOW);

  setupLoRa();
  connectWiFi();
  setupWebServer();

  Serial.println("=== NODE MASTER SAN SANG ===");
}

// ============================================================================
// 8. LOOP CHÍNH - không dùng delay dài, dùng millis()
// ============================================================================
void loop() {
  server.handleClient();
  readLoRaData();
  checkConnectionTimeout();

  // Tắt LED_LORA sau một khoảng ngắn để tạo hiệu ứng nhấp nháy (không chặn chương trình)
  if (loraLedOnTime > 0 && millis() - loraLedOnTime > 150) {
    digitalWrite(LED_LORA_PIN, LOW);
    loraLedOnTime = 0;
  }

  // Theo dõi & tự thử kết nối lại WiFi nếu bị rớt mạng, không chặn vòng lặp
  static unsigned long lastWifiRetry = 0;
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI_PIN, LOW);
    if (millis() - lastWifiRetry > 5000) {
      lastWifiRetry = millis();
      Serial.println("[WIFI] Mat ket noi, dang thu ket noi lai...");
      WiFi.reconnect();
    }
  } else {
    digitalWrite(LED_WIFI_PIN, HIGH);
  }
}

// ============================================================================
// 9. KHỞI TẠO LORA - Normal Mode (M0=LOW, M1=LOW)
// ============================================================================
void setupLoRa() {
  pinMode(LORA_M0_PIN, OUTPUT);
  pinMode(LORA_M1_PIN, OUTPUT);
  pinMode(LORA_AUX_PIN, INPUT);

  digitalWrite(LORA_M0_PIN, LOW);
  digitalWrite(LORA_M1_PIN, LOW);

  loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  delay(100);

  Serial.println("[OK] LoRa E32 da vao Normal Mode (M0=LOW, M1=LOW)");
}

// ============================================================================
// 10. KẾT NỐI WIFI
// ============================================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WIFI] Dang ket noi");

  // Vòng lặp chờ WiFi chỉ chạy 1 lần lúc khởi động (tối đa 15 giây), không
  // ảnh hưởng đến tính thời gian thực của loop() chính bên dưới.
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[OK] WiFi da ket noi! Dia chi IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_WIFI_PIN, HIGH);
  } else {
    Serial.println("[CANH BAO] Chua ket noi duoc WiFi, se tiep tuc thu lai trong loop()");
  }
}

// ============================================================================
// 11. NHẬN DỮ LIỆU LORA TỪ SLAVE
// ============================================================================
void readLoRaData() {
  while (loraSerial.available()) {
    char c = loraSerial.read();
    if (c == '\n') {
      parseDataPacket(rxBuffer);
      rxBuffer = "";
    } else if (c != '\r') {
      rxBuffer += c;
      if (rxBuffer.length() > 200) rxBuffer = ""; // chống tràn bộ đệm nếu nhiễu sóng
    }
  }
}

// Tách chuỗi CSV theo dấu phẩy, kiểm tra đủ số trường trước khi cập nhật biến
void parseDataPacket(String packet) {
  const int EXPECTED_FIELDS = 13;
  String fields[EXPECTED_FIELDS];
  int fieldCount = 0;
  int startIdx = 0;

  for (int i = 0; i <= (int)packet.length(); i++) {
    if (i == (int)packet.length() || packet.charAt(i) == ',') {
      if (fieldCount < EXPECTED_FIELDS) {
        fields[fieldCount] = packet.substring(startIdx, i);
      }
      fieldCount++;
      startIdx = i + 1;
    }
  }

  if (fieldCount != EXPECTED_FIELDS) {
    Serial.println("[LOI] Goi tin LoRa sai dinh dang (so truong=" + String(fieldCount) + "), bo qua goi tin: " + packet);
    return;
  }

  slaveID   = fields[0];
  g_temp    = fields[1].toFloat();
  g_hum     = fields[2].toFloat();
  g_gas     = fields[3].toInt();
  g_light   = fields[4].toInt();
  g_voltage = fields[5].toFloat();
  g_current = fields[6].toFloat();
  g_power   = fields[7].toFloat();
  g_energy  = fields[8].toFloat();
  g_relay1  = fields[9].toInt();
  g_relay2  = fields[10].toInt();
  g_mode    = fields[11];
  g_alarm   = fields[12];

  lastLoRaReceived = millis();
  connectionOK = true;

  digitalWrite(LED_LORA_PIN, HIGH);
  loraLedOnTime = millis();

  Serial.println("[NHAN] " + packet);
}

// Nếu quá thời gian không nhận được dữ liệu -> đánh dấu mất kết nối
void checkConnectionTimeout() {
  if (millis() - lastLoRaReceived > LORA_TIMEOUT_MS) {
    connectionOK = false;
  }
}

// Gửi lệnh điều khiển xuống Slave qua LoRa
void sendCommandToSlave(String cmd) {
  loraSerial.print(cmd + "\n");
  Serial.println("[GUI LENH] " + cmd);
}

// ============================================================================
// 12. KIỂM TRA ĐĂNG NHẬP
// ============================================================================
bool requireLogin() {
  if (!isLoggedIn) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }
  return true;
}

// Dùng cho API AJAX (/api/data, /cmd) - trả JSON 401 thay vì redirect
bool requireLoginJson() {
  if (!isLoggedIn) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Chua dang nhap\"}");
    return false;
  }
  return true;
}

// ============================================================================
// 13. KHỞI TẠO WEB SERVER
// ============================================================================
void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_GET, handleLoginPage);
  server.on("/login", HTTP_POST, handleLoginSubmit);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/home", HTTP_GET, handleHome);
  server.on("/overview", HTTP_GET, handleOverview);
  server.on("/power", HTTP_GET, handlePower);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/cmd", HTTP_GET, handleCmd);
  server.on("/style.css", HTTP_GET, handleStyleCss);
  server.on("/app.js", HTTP_GET, handleAppJs);

  server.onNotFound([]() {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("[OK] Web Server da khoi dong!");
}

// ============================================================================
// 14. HANDLER: ĐĂNG NHẬP / ĐĂNG XUẤT
// ============================================================================
void handleRoot() {
  server.sendHeader("Location", isLoggedIn ? "/home" : "/login");
  server.send(302, "text/plain", "");
}

void handleLoginPage() {
  server.send(200, "text/html", getLoginPageHTML(""));
}

void handleLoginSubmit() {
  String user = server.arg("username");
  String pass = server.arg("password");

  if (user == LOGIN_USER && pass == LOGIN_PASS) {
    isLoggedIn = true;
    server.sendHeader("Location", "/home");
    server.send(302, "text/plain", "");
  } else {
    server.send(200, "text/html", getLoginPageHTML("Sai tài khoản hoặc mật khẩu!"));
  }
}

void handleLogout() {
  isLoggedIn = false;
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "");
}

// ============================================================================
// 15. HANDLER: CÁC TRANG GIAO DIỆN
// ============================================================================
void handleHome() {
  if (!requireLogin()) return;

  String html = getPageStart("Trang chủ", "home");
  html += "<div class=\"hello-card\"><h1>Xin chào! &#128075;</h1>"
          "<p>Chào mừng bạn đến với Hệ thống giám sát và quản lý.</p>"
          "<div class=\"status-row\" style=\"justify-content:center\">"
          "<span class=\"badge\"><span class=\"dot ok\"></span>WiFi: Đã kết nối</span>"
          "<span class=\"badge\"><span class=\"dot\" id=\"lora-dot\"></span><span id=\"lora-label\">LoRa: --</span></span>"
          "</div></div>";
  html += "<div class=\"grid\">"
          "<div class=\"card\"><div class=\"label\">Nhiệt độ</div><div class=\"value\" id=\"temp-value\">-- &deg;C</div></div>"
          "<div class=\"card\"><div class=\"label\">Độ ẩm</div><div class=\"value\" id=\"hum-value\">-- %</div></div>"
          "<div class=\"card accent-cyan\"><div class=\"label\">Điện áp lưới</div><div class=\"value\" id=\"voltage-value\">-- V</div></div>"
          "<div class=\"card\"><div class=\"label\">Công suất</div><div class=\"value\" id=\"power-value\">-- W</div></div>"
          "</div>";
  html += getPageEnd();

  server.send(200, "text/html", html);
}

void handleOverview() {
  if (!requireLogin()) return;

  String html = getPageStart("Tổng quan", "overview");
  html += "<div class=\"grid\">"
          "<div class=\"card\"><div class=\"label\">Nhiệt độ</div><div class=\"value\" id=\"temp-value\">-- &deg;C</div></div>"
          "<div class=\"card\"><div class=\"label\">Độ ẩm</div><div class=\"value\" id=\"hum-value\">-- %</div></div>"
          "<div class=\"card\"><div class=\"label\">Khí gas</div><div class=\"value\" id=\"gas-value\">-- ppm</div></div>"
          "<div class=\"card\"><div class=\"label\">Ánh sáng</div><div class=\"value\" id=\"light-value\">-- lux</div></div>"
          "</div>";
  html += "<div class=\"grid\" style=\"grid-template-columns:1fr 1fr;\">"
          "<div class=\"chart-card\"><div class=\"label\">Nhiệt độ môi trường</div><canvas id=\"chart-temp\" data-color=\"#ef4444\"></canvas></div>"
          "<div class=\"chart-card\"><div class=\"label\">Độ ẩm không khí</div><canvas id=\"chart-hum\" data-color=\"#3b82f6\"></canvas></div>"
          "<div class=\"chart-card\"><div class=\"label\">Nồng độ khí Gas</div><canvas id=\"chart-gas\" data-color=\"#22c55e\"></canvas></div>"
          "<div class=\"chart-card\"><div class=\"label\">Cường độ ánh sáng</div><canvas id=\"chart-light\" data-color=\"#f59e0b\"></canvas></div>"
          "</div>";
  html += getPageEnd();

  server.send(200, "text/html", html);
}

void handlePower() {
  if (!requireLogin()) return;

  String html = getPageStart("Điện năng", "power");
  html += "<div class=\"grid\">"
          "<div class=\"card\"><div class=\"label\">Điện áp lưới</div><div class=\"value\" id=\"voltage-value\">-- V</div></div>"
          "<div class=\"card\"><div class=\"label\">Dòng điện</div><div class=\"value\" id=\"current-value\">-- A</div></div>"
          "<div class=\"card\"><div class=\"label\">Công suất</div><div class=\"value\" id=\"power-value\">-- W</div></div>"
          "<div class=\"card\"><div class=\"label\">Điện năng tiêu thụ</div><div class=\"value small-unit\" id=\"energy-value\">-- kWh</div></div>"
          "</div>";
  html += "<div class=\"grid\" style=\"grid-template-columns:1fr 1fr;\">"
          "<div class=\"chart-card\"><div class=\"label\">Biểu đồ Công suất tiêu thụ</div><canvas id=\"chart-power\" data-color=\"#ef4444\"></canvas></div>"
          "<div class=\"chart-card\"><div class=\"label\">Biểu đồ Điện áp lưới</div><canvas id=\"chart-voltage\" data-color=\"#f59e0b\"></canvas></div>"
          "</div>";
  html += getPageEnd();

  server.send(200, "text/html", html);
}

void handleControl() {
  if (!requireLogin()) return;

  String html = getPageStart("Điều khiển", "control");
  html += "<div class=\"panel\">";
  html += "<div class=\"row-between\"><span>Chế độ hiện tại</span><b id=\"control-mode-text\">--</b></div>";
  html += "<div class=\"btn-group\">"
          "<button class=\"btn\" id=\"btn-auto\" onclick=\"sendCmd('SET_AUTO')\">AUTO</button>"
          "<button class=\"btn\" id=\"btn-manual\" onclick=\"sendCmd('SET_MANUAL')\">MANUAL</button>"
          "</div>";
  html += "<p class=\"mode-note\" id=\"control-note\"></p>";

  html += "<div class=\"row-between\"><span>Relay 1 (Ổ cắm / tải)</span><b id=\"control-r1-text\">--</b></div>";
  html += "<div class=\"btn-group\">"
          "<button class=\"btn on\" id=\"btn-r1-on\" onclick=\"sendCmd('RELAY1_ON')\">BẬT</button>"
          "<button class=\"btn off\" id=\"btn-r1-off\" onclick=\"sendCmd('RELAY1_OFF')\">TẮT</button>"
          "</div>";

  html += "<div class=\"row-between\"><span>Relay 2 (Đèn)</span><b id=\"control-r2-text\">--</b></div>";
  html += "<div class=\"btn-group\">"
          "<button class=\"btn on\" id=\"btn-r2-on\" onclick=\"sendCmd('RELAY2_ON')\">BẬT ĐÈN</button>"
          "<button class=\"btn off\" id=\"btn-r2-off\" onclick=\"sendCmd('RELAY2_OFF')\">TẮT ĐÈN</button>"
          "</div>";

  html += "<div class=\"btn-group\">"
          "<button class=\"btn\" style=\"background:#f59e0b\" onclick=\"sendCmd('BUZZER_OFF')\">TẮT CÒI</button>"
          "<button class=\"btn\" style=\"background:#8b5cf6\" onclick=\"sendCmd('RESET_ALARM')\">XÓA CẢNH BÁO</button>"
          "</div>";
  html += "</div>";
  html += getPageEnd();

  server.send(200, "text/html", html);
}

// ============================================================================
// 16. HANDLER: API JSON (dữ liệu realtime)
// ============================================================================
void handleApiData() {
  if (!requireLoginJson()) return;

  String json = "{";
  json += "\"temp\":" + String(g_temp, 1) + ",";
  json += "\"hum\":" + String(g_hum, 1) + ",";
  json += "\"gas\":" + String(g_gas) + ",";
  json += "\"light\":" + String(g_light) + ",";
  json += "\"voltage\":" + String(g_voltage, 1) + ",";
  json += "\"current\":" + String(g_current, 2) + ",";
  json += "\"power\":" + String(g_power, 1) + ",";
  json += "\"energy\":" + String(g_energy, 3) + ",";
  json += "\"relay1\":" + String(g_relay1) + ",";
  json += "\"relay2\":" + String(g_relay2) + ",";
  json += "\"mode\":\"" + g_mode + "\",";
  json += "\"alarm\":\"" + g_alarm + "\",";
  json += "\"connected\":" + String(connectionOK ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

// ============================================================================
// 17. HANDLER: GỬI LỆNH ĐIỀU KHIỂN (/cmd?value=...)
// ============================================================================
void handleCmd() {
  if (!requireLoginJson()) return;

  if (!server.hasArg("value")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Thieu tham so value\"}");
    return;
  }

  String cmd = server.arg("value");

  // Danh sách lệnh hợp lệ - tránh phát lệnh rác qua sóng LoRa
  bool valid = (cmd == "SET_AUTO" || cmd == "SET_MANUAL" ||
                cmd == "RELAY1_ON" || cmd == "RELAY1_OFF" ||
                cmd == "RELAY2_ON" || cmd == "RELAY2_OFF" ||
                cmd == "BUZZER_OFF" || cmd == "RESET_ALARM");

  if (!valid) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Lenh khong hop le\"}");
    return;
  }

  sendCommandToSlave(cmd);
  server.send(200, "application/json", "{\"status\":\"ok\",\"cmd\":\"" + cmd + "\"}");
}

// ============================================================================
// 18. HANDLER: FILE TĨNH CSS / JS DÙNG CHUNG
// ============================================================================
void handleStyleCss() {
  server.send(200, "text/css", getStyleCSS());
}

void handleAppJs() {
  server.send(200, "application/javascript", getAppJS());
}

// ============================================================================
// 19. HTML: THANH ĐIỀU HƯỚNG DÙNG CHUNG
// ============================================================================
String getNavbar(String active) {
  String nav = "<div class=\"topbar\"><div class=\"brand\">ĐH CÔNG NGHỆ KỸ THUẬT TP.HCM"
               "<small>Hệ thống quản lý tiêu thụ điện - môi trường từ xa</small></div>"
               "<div class=\"clock\" id=\"clock\">--:--:--</div></div>";

  nav += "<div class=\"navbar\">";
  nav += "<a href=\"/home\" class=\"" + String(active == "home" ? "active" : "") + "\">TRANG CHỦ</a>";
  nav += "<a href=\"/overview\" class=\"" + String(active == "overview" ? "active" : "") + "\">TỔNG QUAN</a>";
  nav += "<a href=\"/power\" class=\"" + String(active == "power" ? "active" : "") + "\">ĐIỆN NĂNG</a>";
  nav += "<a href=\"/control\" class=\"" + String(active == "control" ? "active" : "") + "\">ĐIỀU KHIỂN</a>";
  nav += "<a href=\"/logout\" class=\"logout\">ĐĂNG XUẤT</a>";
  nav += "</div>";
  return nav;
}

String getPageStart(String title, String active) {
  String html = "<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>" + title + "</title>"
                "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>";
  html += getNavbar(active);
  html += "<div class=\"container\">";
  html += "<div id=\"alarm-banner\" class=\"alert-banner\"><h2 id=\"alarm-title\"></h2><p id=\"alarm-desc\"></p></div>";
  return html;
}

String getPageEnd() {
  return "</div><script src=\"/app.js\"></script></body></html>";
}

// ============================================================================
// 20. HTML: TRANG ĐĂNG NHẬP
// ============================================================================
String getLoginPageHTML(String errorMsg) {
  String html = R"rawliteral(<!DOCTYPE html>
<html lang="vi"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Đăng nhập</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div class="login-wrap"><div class="login-box">
<h1>ĐĂNG NHẬP</h1>)rawliteral";

  if (errorMsg.length() > 0) {
    html += "<div class=\"login-error\">" + errorMsg + "</div>";
  }

  html += R"rawliteral(
<form action="/login" method="POST">
<label>Tài khoản</label>
<input type="text" name="username" autocomplete="username" required>
<label>Mật khẩu</label>
<input type="password" name="password" autocomplete="current-password" required>
<button type="submit">ĐĂNG NHẬP</button>
</form>
</div></div>
</body></html>)rawliteral";

  return html;
}

// ============================================================================
// 21. CSS DÙNG CHUNG (giao diện dashboard IoT: nền tối, xanh cyan)
// ============================================================================
String getStyleCSS() {
  return R"rawliteral(
:root{
  --bg-dark:#0b1622; --bg-panel:#142433; --bg-card:#16283a;
  --accent:#22d3ee; --accent2:#3b82f6; --text-main:#e6f1f5; --text-dim:#8ea3b0;
  --green:#22c55e; --red:#ef4444; --gray:#64748b; --line:#1e3a4c;
}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(160deg,#0b1622 0%,#0f2536 100%);
  color:var(--text-main);min-height:100vh;}
.topbar{display:flex;justify-content:space-between;align-items:center;padding:14px 24px;
  background:var(--bg-panel);border-bottom:1px solid var(--line);}
.topbar .brand{font-weight:700;font-size:15px;}
.topbar .brand small{display:block;font-weight:400;font-size:11px;color:var(--text-dim);margin-top:2px;}
.topbar .clock{font-size:12px;color:var(--text-dim);text-align:right;}
.navbar{display:flex;gap:4px;padding:0 24px;background:var(--bg-panel);
  border-bottom:2px solid var(--line);overflow-x:auto;}
.navbar a{color:var(--text-dim);text-decoration:none;padding:12px 16px;font-size:13px;
  font-weight:600;letter-spacing:.5px;white-space:nowrap;border-bottom:3px solid transparent;}
.navbar a.active,.navbar a:hover{color:var(--accent);border-bottom-color:var(--accent);}
.navbar .logout{margin-left:auto;color:var(--red);}
.container{padding:24px;max-width:1100px;margin:0 auto;}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:16px;margin-bottom:20px;}
.card{background:var(--bg-card);border-radius:14px;padding:18px;box-shadow:0 4px 14px rgba(0,0,0,.35);
  border:1px solid var(--line);}
.card .label{font-size:11px;color:var(--text-dim);text-transform:uppercase;letter-spacing:.5px;}
.card .value{font-size:26px;font-weight:700;margin-top:6px;}
.card .value.small-unit{font-size:22px;}
.card.accent-cyan .value{color:var(--accent);}
.chart-card{background:var(--bg-card);border-radius:14px;padding:16px;border:1px solid var(--line);margin-bottom:16px;}
.chart-card .label{font-size:12px;color:var(--text-dim);margin-bottom:8px;}
canvas{width:100%;height:200px;display:block;}
.status-row{display:flex;gap:10px;align-items:center;margin-bottom:18px;flex-wrap:wrap;}
.badge{display:inline-flex;align-items:center;gap:6px;background:var(--bg-card);border:1px solid var(--line);
  border-radius:20px;padding:6px 14px;font-size:12px;}
.dot{width:8px;height:8px;border-radius:50%;background:var(--gray);}
.dot.ok{background:var(--green);}
.dot.bad{background:var(--red);}
.alert-banner{display:none;background:linear-gradient(135deg,#ef4444,#b91c1c);border-radius:14px;
  padding:24px;text-align:center;margin-bottom:20px;}
.alert-banner h2{font-size:20px;margin-bottom:6px;}
.alert-banner p{font-size:13px;opacity:.9;}
.btn{border:none;border-radius:10px;padding:12px 20px;font-size:13px;font-weight:700;cursor:pointer;
  color:#fff;background:var(--gray);transition:transform .1s;}
.btn:active{transform:scale(.97);}
.btn.primary{background:var(--accent2);}
.btn.on{background:var(--green);}
.btn.off{background:var(--red);}
.btn:disabled{opacity:.4;cursor:not-allowed;}
.panel{background:var(--bg-card);border-radius:14px;padding:24px;border:1px solid var(--line);
  max-width:480px;margin:0 auto;}
.row-between{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px;font-size:14px;}
.row-between b{color:var(--accent);}
.btn-group{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px;}
.login-wrap{min-height:100vh;display:flex;align-items:center;justify-content:center;}
.login-box{background:var(--bg-card);padding:36px 32px;border-radius:18px;width:320px;
  box-shadow:0 10px 40px rgba(0,0,0,.5);border:1px solid var(--line);}
.login-box h1{text-align:center;color:var(--accent);font-size:20px;letter-spacing:1px;margin-bottom:24px;}
.login-box label{font-size:12px;color:var(--text-dim);display:block;margin-bottom:6px;}
.login-box input{width:100%;padding:10px 12px;margin-bottom:16px;border-radius:8px;border:1px solid var(--line);
  background:#0b1622;color:var(--text-main);}
.login-box button{width:100%;padding:12px;border:none;border-radius:8px;background:var(--accent2);
  color:#fff;font-weight:700;cursor:pointer;}
.login-error{color:var(--red);font-size:12px;text-align:center;margin-bottom:12px;}
.hello-card{text-align:center;padding:40px 20px;}
.hello-card h1{font-size:26px;margin-bottom:8px;}
.hello-card p{color:var(--text-dim);margin-bottom:24px;font-size:13px;}
.mode-note{font-size:11px;color:var(--text-dim);margin-top:-6px;margin-bottom:14px;min-height:14px;}
)rawliteral";
}

// ============================================================================
// 22. JAVASCRIPT DÙNG CHUNG (fetch dữ liệu, vẽ biểu đồ Canvas, gửi lệnh)
// ============================================================================
String getAppJS() {
  return R"rawliteral(
function setText(id, text){ var el=document.getElementById(id); if(el) el.innerText=text; }

function fetchData(){
  fetch('/api/data').then(function(r){ return r.json(); }).then(function(d){
    setText('temp-value', d.temp.toFixed(1) + ' \u00B0C');
    setText('hum-value', d.hum.toFixed(1) + ' %');
    setText('gas-value', d.gas + ' ppm');
    setText('light-value', d.light + ' lux');
    setText('voltage-value', d.voltage.toFixed(1) + ' V');
    setText('current-value', d.current.toFixed(2) + ' A');
    setText('power-value', d.power.toFixed(1) + ' W');
    setText('energy-value', d.energy.toFixed(3) + ' kWh');

    updateConnBadge(d.connected);
    updateAlarmBanner(d.alarm, d.connected);
    updateControlUI(d.mode, d.relay1, d.relay2);

    pushChart('chart-temp', d.temp);
    pushChart('chart-hum', d.hum);
    pushChart('chart-gas', d.gas);
    pushChart('chart-light', d.light);
    pushChart('chart-power', d.power);
    pushChart('chart-voltage', d.voltage);
  }).catch(function(e){ console.log('Loi lay du lieu:', e); });
}

function updateConnBadge(connected){
  var dot = document.getElementById('lora-dot');
  var label = document.getElementById('lora-label');
  if (dot) dot.className = 'dot ' + (connected ? 'ok' : 'bad');
  if (label) label.innerText = connected ? 'LoRa: Đã kết nối' : 'LoRa: Mất kết nối';
}

function updateAlarmBanner(alarm, connected){
  var banner = document.getElementById('alarm-banner');
  if (!banner) return;
  var title = document.getElementById('alarm-title');
  var desc = document.getElementById('alarm-desc');

  if (!connected){
    banner.style.display = 'block';
    if (title) title.innerText = 'CẢNH BÁO: MẤT KẾT NỐI LORA';
    if (desc) desc.innerText = 'Không nhận được dữ liệu từ Node Slave quá thời gian cho phép.';
    return;
  }
  if (alarm && alarm !== 'NORMAL'){
    banner.style.display = 'block';
    if (alarm === 'GAS_ALARM'){
      if (title) title.innerText = 'CẢNH BÁO KHÍ GAS';
      if (desc) desc.innerText = 'Phát hiện rò rỉ khí gas! Hệ thống đã ngắt relay để bảo vệ.';
    } else if (alarm === 'OVERLOAD'){
      if (title) title.innerText = 'CẢNH BÁO QUÁ TẢI';
      if (desc) desc.innerText = 'Công suất/dòng điện vượt ngưỡng! Hệ thống đã ngắt relay để bảo vệ.';
    } else if (alarm === 'SENSOR_ERROR'){
      if (title) title.innerText = 'LỖI CẢM BIẾN';
      if (desc) desc.innerText = 'Không đọc được dữ liệu từ cảm biến, vui lòng kiểm tra phần cứng.';
    }
  } else {
    banner.style.display = 'none';
  }
}

function updateControlUI(mode, relay1, relay2){
  var autoBtn = document.getElementById('btn-auto');
  var manBtn = document.getElementById('btn-manual');
  var r1on = document.getElementById('btn-r1-on');
  var r1off = document.getElementById('btn-r1-off');
  var r2on = document.getElementById('btn-r2-on');
  var r2off = document.getElementById('btn-r2-off');
  var modeText = document.getElementById('control-mode-text');
  var r1Text = document.getElementById('control-r1-text');
  var r2Text = document.getElementById('control-r2-text');
  var note = document.getElementById('control-note');

  if (modeText) modeText.innerText = mode;
  if (r1Text){ r1Text.innerText = (relay1 == 1) ? 'BẬT' : 'TẮT'; r1Text.style.color = (relay1 == 1) ? '#22c55e' : '#ef4444'; }
  if (r2Text){ r2Text.innerText = (relay2 == 1) ? 'BẬT' : 'TẮT'; r2Text.style.color = (relay2 == 1) ? '#22c55e' : '#ef4444'; }
  if (autoBtn) autoBtn.classList.toggle('primary', mode === 'AUTO');
  if (manBtn) manBtn.classList.toggle('primary', mode === 'MANUAL');

  var manual = (mode === 'MANUAL');
  [r1on, r1off, r2on, r2off].forEach(function(b){ if (b) b.disabled = !manual; });
  if (note) note.innerText = manual ? '' : 'Đang điều khiển tự động (AUTO) - các nút Relay bị khóa';
}

function sendCmd(value){
  fetch('/cmd?value=' + encodeURIComponent(value))
    .then(function(r){ return r.json(); })
    .then(function(d){ console.log(d); fetchData(); })
    .catch(function(e){ console.log('Loi gui lenh:', e); });
}

// ==== Vẽ biểu đồ đường đơn giản bằng Canvas (không cần Internet/CDN) ====
var chartData = {};
var MAX_POINTS = 30;

function pushChart(canvasId, value){
  var canvas = document.getElementById(canvasId);
  if (!canvas) return;
  if (!chartData[canvasId]) chartData[canvasId] = [];
  chartData[canvasId].push(value);
  if (chartData[canvasId].length > MAX_POINTS) chartData[canvasId].shift();
  drawChart(canvas, chartData[canvasId]);
}

function drawChart(canvas, data){
  var dpr = window.devicePixelRatio || 1;
  var w = canvas.clientWidth, h = canvas.clientHeight;
  canvas.width = w * dpr; canvas.height = h * dpr;
  var ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.clearRect(0, 0, w, h);
  if (data.length < 2) return;

  var min = Math.min.apply(null, data);
  var max = Math.max.apply(null, data);
  if (min == max){ min -= 1; max += 1; }
  var pad = 10;
  var color = canvas.getAttribute('data-color') || '#22d3ee';

  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  data.forEach(function(v, i){
    var x = pad + (i / (data.length - 1)) * (w - pad * 2);
    var y = h - pad - ((v - min) / (max - min)) * (h - pad * 2);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();

  ctx.lineTo(w - pad, h - pad);
  ctx.lineTo(pad, h - pad);
  ctx.closePath();
  ctx.fillStyle = color + '22';
  ctx.fill();
}

var clockEl = document.getElementById('clock');
if (clockEl){
  setInterval(function(){
    clockEl.innerText = new Date().toLocaleTimeString('vi-VN');
  }, 1000);
}

setInterval(fetchData, 2000);
window.addEventListener('load', fetchData);
)rawliteral";
}
