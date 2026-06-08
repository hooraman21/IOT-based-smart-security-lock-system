// ============================================================
//   SMART DOOR LOCK SYSTEM — ESP32
//   Components: LCD I2C, Keypad 4x4, Relay, Solenoid Lock, LEDs
//   Features:
//     ✅ Password entry via keypad
//     ✅ WiFi Web Dashboard (remote unlock/lock)
//     ✅ Bluetooth (Serial Bluetooth Terminal app)
//     ✅ Wrong password alerts (WiFi log + Bluetooth notification)
//     ✅ Entry log (last 20 events)
//     ✅ Green LED = Door Unlocked
//     ✅ Red LED   = Door Locked
//     ✅ Red LED blink = Wrong password
//     ✅ Red LED fast blink = System Blocked
//
//   WIRING:
//   LCD SDA      → GPIO 21
//   LCD SCL      → GPIO 22
//   Relay IN     → GPIO 4
//   GREEN LED    → GPIO 18  (220Ω resistor → GND)
//   RED LED      → GPIO 19  (220Ω resistor → GND)
//   Keypad Row1  → GPIO 25
//   Keypad Row2  → GPIO 26
//   Keypad Row3  → GPIO 32
//   Keypad Row4  → GPIO 33
//   Keypad Col1  → GPIO 13
//   Keypad Col2  → GPIO 12
//   Keypad Col3  → GPIO 14
//   Keypad Col4  → GPIO 27
//
//   BLUETOOTH COMMANDS (Serial Bluetooth Terminal app):
//   Send:  U   → Unlock door
//   Send:  L   → Lock door
//   Send:  S   → Get status
//   Send:  R   → Reset wrong attempts
//   Send:  H   → Show all commands (Help)
//
//   KEYPAD BUTTONS:
//   0-9,*,#,B  → Password characters
//   D          → Submit password
//   C          → Clear input
//   A          → Lock door manually
// ============================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>
#include <time.h>

// ─────────────────────────────────────────
//  *** SIRF YEH 3 LINES CHANGE KARAIN ***
// ─────────────────────────────────────────
const char* WIFI_SSID      = "Smart-Campus-STD";
const char* WIFI_PASSWORD  = "pafiast##std2021";
char        DOOR_PASSWORD[] = "1414*";
// ─────────────────────────────────────────

#define RELAY_PIN      4
#define LED_GREEN      18    // ← GREEN LED pin (NEW)
#define LED_RED        19    // ← RED LED pin   (NEW)
#define MAX_LOGS       20
#define BT_DEVICE_NAME "SmartDoorLock"

// ─────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
BluetoothSerial   SerialBT;
WebServer         server(80);

// ─────────────────────────────────────────
//  KEYPAD
// ─────────────────────────────────────────
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','4','7','*'},
  {'2','5','8','0'},
  {'3','6','9','#'},
  {'A','B','C','D'}
};

byte rowPins[ROWS] = {25, 26, 32, 33};
byte colPins[COLS]  = {13, 12, 14, 27};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ─────────────────────────────────────────
//  PASSWORD CLASS
// ─────────────────────────────────────────
#define MAX_PASSWORD_LENGTH 20
#define STRING_TERMINATOR   '\0'

class Password {
public:
  Password(char* pass) { set(pass); reset(); }

  void set(char* pass) { target = pass; }

  bool append(char character) {
    if (currentIndex + 1 >= MAX_PASSWORD_LENGTH) return false;
    guess[currentIndex++] = character;
    guess[currentIndex]   = STRING_TERMINATOR;
    return true;
  }

  void reset() {
    currentIndex        = 0;
    guess[currentIndex] = STRING_TERMINATOR;
  }

  bool evaluate() {
    int i = 0;
    while (target[i] != STRING_TERMINATOR || guess[i] != STRING_TERMINATOR) {
      if (target[i] != guess[i]) return false;
      i++;
    }
    return true;
  }

  String getGuess() { return String(guess); }

private:
  char* target;
  char  guess[MAX_PASSWORD_LENGTH];
  byte  currentIndex;
};

Password password = Password(DOOR_PASSWORD);

// ─────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────
byte currentPasswordLength = 0;
byte lcdPos                = 5;
int  wrongAttempts         = 0;
bool permanentLock         = false;
bool isDoorLocked          = true;
bool remoteUnlockPending   = false;
bool remoteLockPending     = false;

// ─────────────────────────────────────────
//  ✅ LED FUNCTIONS (NEW)
// ─────────────────────────────────────────

// Door LOCKED → Red ON, Green OFF
void ledLocked() {
  digitalWrite(LED_RED,   HIGH);
  digitalWrite(LED_GREEN, LOW);
}

// Door UNLOCKED → Green ON, Red OFF
void ledUnlocked() {
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED,   LOW);
}

// Wrong password → Red blink 3 times slowly
void ledWrongBlink() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_RED, LOW);
    delay(150);
    digitalWrite(LED_RED, HIGH);
    delay(150);
  }
  ledLocked(); // return to locked state
}

// System blocked → Red fast blink (call in loop)
void ledBlockedBlink() {
  digitalWrite(LED_RED, HIGH);
  delay(80);
  digitalWrite(LED_RED, LOW);
  delay(80);
}

// ─────────────────────────────────────────
//  LOG SYSTEM
// ─────────────────────────────────────────
struct LogEntry {
  char message[50];
  char timestamp[20];
};

LogEntry logs[MAX_LOGS];
int logIndex  = 0;
int totalLogs = 0;

void syncTime() {
  configTime(5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:-- --/--";
  char buf[20];
  strftime(buf, sizeof(buf), "%H:%M %d/%m/%Y", &timeinfo);
  return String(buf);
}

void addLog(String msg) {
  String t = getTimestamp();
  strncpy(logs[logIndex].message,   msg.c_str(), 49);
  strncpy(logs[logIndex].timestamp, t.c_str(),   19);
  logs[logIndex].message[49]   = '\0';
  logs[logIndex].timestamp[19] = '\0';
  logIndex = (logIndex + 1) % MAX_LOGS;
  if (totalLogs < MAX_LOGS) totalLogs++;
}

// ─────────────────────────────────────────
//  BLUETOOTH SEND HELPER
// ─────────────────────────────────────────
void btSend(String msg) {
  if (SerialBT.hasClient()) {
    SerialBT.println(msg);
  }
  Serial.println("[BT] " + msg);
}

void btAlert(String msg) {
  btSend("==============================");
  btSend("  " + msg);
  btSend("==============================");
}

// ─────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────
void lockDoor();
void resetPassword();

// ─────────────────────────────────────────
//  DOOR CONTROL
// ─────────────────────────────────────────
void unlockDoor(String reason) {
  digitalWrite(RELAY_PIN, LOW);   // LOW = relay ON = solenoid OPENS
  isDoorLocked  = false;
  wrongAttempts = 0;
  ledUnlocked();                  // ✅ Green ON, Red OFF (NEW)

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ACCESS GRANTED  ");
  lcd.setCursor(0, 1);
  lcd.print("DOOR OPENED     ");

  addLog("Unlocked - " + reason);
  btSend("[UNLOCK] Door opened via " + reason);

  unsigned long t = millis();
  while (millis() - t < 2000) { server.handleClient(); }
}

void lockDoor() {
  digitalWrite(RELAY_PIN, HIGH);  // HIGH = relay OFF = solenoid LOCKS
  isDoorLocked = true;
  ledLocked();                    // ✅ Red ON, Green OFF (NEW)

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DOOR LOCKED     ");
  lcd.setCursor(0, 1);
  lcd.print("                ");

  addLog("Door locked");
  btSend("[LOCKED] Door is now locked");

  unsigned long t = millis();
  while (millis() - t < 2000) { server.handleClient(); }
  resetPassword();
}

// ─────────────────────────────────────────
//  PASSWORD HANDLING
// ─────────────────────────────────────────
void checkPassword() {
  Serial.print("Password entered: ");
  Serial.println(password.getGuess());

  if (password.evaluate()) {
    unlockDoor("keypad");
  } else {
    wrongAttempts++;
    addLog("Wrong password attempt #" + String(wrongAttempts));

    ledWrongBlink();               // ✅ Red blink on wrong password (NEW)

    btAlert("! WRONG PASSWORD !");
    btSend("Attempt: " + String(wrongAttempts) + " / 3");
    btSend("Time: " + getTimestamp());

    if (wrongAttempts >= 4) {
      permanentLock = true;
      addLog("SYSTEM BLOCKED - too many wrong attempts");
      btAlert("!! SYSTEM BLOCKED !!");
      btSend("Too many wrong attempts.");
      btSend("Use web dashboard to reset.");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("SYSTEM BLOCKED  ");
      lcd.setCursor(0, 1);
      lcd.print("NO ACCESS       ");
      resetPassword();
      return;
    }

    int lockTime = 0;
    if      (wrongAttempts == 1) lockTime = 10;
    else if (wrongAttempts == 2) lockTime = 20;
    else if (wrongAttempts == 3) lockTime = 30;

    btSend("Locked for " + String(lockTime) + " seconds...");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WRONG PASSWORD  ");
    lcd.setCursor(0, 1);
    lcd.print("WAIT " + String(lockTime) + "s          ");

    unsigned long start = millis();
    while (millis() - start < (unsigned long)(lockTime * 1000UL)) {
      server.handleClient();
      int remaining = lockTime - (int)((millis() - start) / 1000);
      lcd.setCursor(5, 1);
      lcd.print(String(remaining) + "s  ");
      delay(200);
    }

    ledLocked();                   // ✅ Back to red after wait (NEW)
  }
  resetPassword();
}

void processKey(char key) {
  if (currentPasswordLength < MAX_PASSWORD_LENGTH) {
    lcd.setCursor(lcdPos, 1);
    lcd.print("*");
    lcdPos++;
    currentPasswordLength++;
    password.append(key);
  }
}

void resetPassword() {
  password.reset();
  currentPasswordLength = 0;
  lcdPos                = 5;
  lcd.clear();
}

// ─────────────────────────────────────────
//  BLUETOOTH COMMAND HANDLER
// ─────────────────────────────────────────
void handleBluetooth() {
  if (!SerialBT.available()) return;

  char cmd = SerialBT.read();
  while (SerialBT.available()) SerialBT.read();

  Serial.println("[BT] Command received: " + String(cmd));

  switch (cmd) {

    case 'U': case 'u':
      if (permanentLock) {
        btSend("[ERROR] System is BLOCKED. Reset first.");
      } else if (!isDoorLocked) {
        btSend("[INFO] Door is already open.");
      } else {
        btSend("[OK] Unlocking door via Bluetooth...");
        unlockDoor("Bluetooth");
      }
      break;

    case 'L': case 'l':
      if (isDoorLocked) {
        btSend("[INFO] Door is already locked.");
      } else {
        btSend("[OK] Locking door via Bluetooth...");
        addLog("Locked - Bluetooth");
        lockDoor();
      }
      break;

    case 'S': case 's': {
      btSend("--- DOOR STATUS ---");
      btSend("Door    : " + String(isDoorLocked ? "LOCKED" : "UNLOCKED"));
      btSend("System  : " + String(permanentLock ? "BLOCKED" : "ACTIVE"));
      btSend("Attempts: " + String(wrongAttempts) + " / 3");
      btSend("Events  : " + String(totalLogs));
      btSend("Time    : " + getTimestamp());
      btSend("-------------------");
      break;
    }

    case 'R': case 'r':
      wrongAttempts = 0;
      permanentLock = false;
      addLog("Reset via Bluetooth");
      ledLocked();                 // ✅ Reset LED to locked state (NEW)
      btSend("[OK] Wrong attempts reset. System unblocked.");
      break;

    case 'H': case 'h': case '?':
      btSend("=== COMMANDS ===");
      btSend("U = Unlock door");
      btSend("L = Lock door");
      btSend("S = Status");
      btSend("R = Reset attempts");
      btSend("H = Help");
      btSend("================");
      break;

    default:
      btSend("[?] Unknown command. Send H for help.");
      break;
  }
}

// ─────────────────────────────────────────
//  WEB DASHBOARD
// ─────────────────────────────────────────
void handleRoot() {
  String statusColor = isDoorLocked ? "#e74c3c" : "#2ecc71";
  String statusText  = isDoorLocked ? "LOCKED"  : "UNLOCKED";
  String statusEmoji = isDoorLocked ? "&#128274;" : "&#128275;";

  String html = "";
  html += "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='10'>";
  html += "<title>Smart Door Lock</title>";
  html += "<style>";
  html += "@import url('https://fonts.googleapis.com/css2?family=Orbitron:wght@700;900&family=Share+Tech+Mono&display=swap');";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{font-family:'Share Tech Mono',monospace;background:#080c12;color:#c8d6e5;padding:20px;min-height:100vh}";
  html += "h1{font-family:'Orbitron',sans-serif;font-size:1.5em;font-weight:900;color:#00d2ff;text-align:center;letter-spacing:5px;text-shadow:0 0 25px #00d2ffaa;margin-bottom:4px}";
  html += ".sub{text-align:center;font-size:.7em;color:#334e72;letter-spacing:3px;margin-bottom:24px}";
  html += ".status-box{text-align:center;font-family:'Orbitron',sans-serif;font-size:1.5em;font-weight:900;padding:20px;border-radius:14px;margin:0 auto 20px;max-width:360px;letter-spacing:4px;border:2px solid}";
  html += ".card{background:#0e1520;border:1px solid #1a3355;border-radius:14px;padding:20px;max-width:500px;margin:0 auto 18px}";
  html += ".card h3{font-family:'Orbitron',sans-serif;font-size:.78em;color:#00d2ff;letter-spacing:3px;margin-bottom:14px;border-bottom:1px solid #1a3355;padding-bottom:9px}";
  html += ".row{display:flex;justify-content:space-between;margin-bottom:9px;font-size:.85em}";
  html += ".lbl{color:#334e72}.val{font-weight:bold;color:#c8d6e5}";
  html += ".val.g{color:#2ecc71}.val.r{color:#e74c3c}.val.b{color:#00d2ff}";
  html += "button{font-family:'Share Tech Mono',monospace;border:none;padding:14px;border-radius:9px;cursor:pointer;font-size:.9em;letter-spacing:1px;width:100%;margin-bottom:10px;transition:opacity .2s}";
  html += "button:hover{opacity:.8}";
  html += ".bu{background:linear-gradient(135deg,#00d2ff,#0055ff);color:#000;font-weight:bold}";
  html += ".bl{background:linear-gradient(135deg,#e74c3c,#9b1a1a);color:#fff}";
  html += ".br{background:#1a3355;color:#00d2ff;border:1px solid #00d2ff33}";
  html += ".log-row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #141e2e;font-size:.78em;gap:12px}";
  html += ".log-row:last-child{border-bottom:none}";
  html += ".lt{color:#334e72;white-space:nowrap;flex-shrink:0}";
  html += ".lm{color:#c8d6e5}.lm.w{color:#f39c12}.lm.g{color:#2ecc71}.lm.b{color:#00d2ff}.lm.r{color:#e74c3c}";
  html += ".blocked{background:#5a0a0a;text-align:center;padding:13px;border-radius:9px;font-family:'Orbitron',sans-serif;font-size:.78em;letter-spacing:2px;color:#ff6b6b;margin-bottom:16px;animation:pulse 1.5s infinite}";
  html += "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}";
  html += ".bt-badge{background:#1a3355;border:1px solid #00d2ff44;border-radius:8px;padding:8px 14px;text-align:center;font-size:.78em;color:#00d2ff;margin-bottom:4px}";
  html += ".led-row{display:flex;gap:20px;justify-content:center;margin:10px 0}";
  html += ".led-dot{width:14px;height:14px;border-radius:50%;display:inline-block;margin-right:6px;vertical-align:middle}";
  html += ".empty{color:#334e72;text-align:center;padding:12px;font-size:.82em}";
  html += ".foot{text-align:center;font-size:.68em;color:#1e3050;margin-top:16px}";
  html += "</style></head><body>";

  html += "<h1>DOOR LOCK</h1>";
  html += "<p class='sub'>SMART ACCESS CONTROL v2.0</p>";

  if (permanentLock) {
    html += "<div class='blocked'>&#9940; SYSTEM PERMANENTLY BLOCKED &#9940;</div>";
  }

  // Status box
  html += "<div class='status-box' style='color:" + statusColor + ";border-color:" + statusColor + ";box-shadow:0 0 28px " + statusColor + "44'>";
  html += statusEmoji + "&nbsp;" + statusText + "</div>";

  // ✅ LED status indicators on dashboard (NEW)
  html += "<div class='led-row'>";
  html += "<span><span class='led-dot' style='background:" + String(!isDoorLocked ? "#2ecc71" : "#1a3355") + ";box-shadow:" + String(!isDoorLocked ? "0 0 8px #2ecc71" : "none") + "'></span>Green LED</span>";
  html += "<span><span class='led-dot' style='background:" + String(isDoorLocked ? "#e74c3c" : "#1a3355") + ";box-shadow:" + String(isDoorLocked ? "0 0 8px #e74c3c" : "none") + "'></span>Red LED</span>";
  html += "</div>";

  // Stats card
  html += "<div class='card'><h3>&#128202; STATUS</h3>";
  html += "<div class='row'><span class='lbl'>Door</span><span class='val " + String(isDoorLocked?"r":"g") + "'>" + statusText + "</span></div>";
  html += "<div class='row'><span class='lbl'>Wrong Attempts</span><span class='val " + String(wrongAttempts>0?"r":"g") + "'>" + String(wrongAttempts) + " / 3</span></div>";
  html += "<div class='row'><span class='lbl'>System</span><span class='val " + String(permanentLock?"r":"g") + "'>" + String(permanentLock?"BLOCKED":"ACTIVE") + "</span></div>";
  html += "<div class='row'><span class='lbl'>Total Events</span><span class='val'>" + String(totalLogs) + "</span></div>";
  html += "<div class='row'><span class='lbl'>WiFi IP</span><span class='val b'>" + WiFi.localIP().toString() + "</span></div>";
  html += "<div class='row'><span class='lbl'>Bluetooth</span><span class='val b'>" + String(BT_DEVICE_NAME) + "</span></div>";
  html += "</div>";

  // Bluetooth info card
  html += "<div class='card'><h3>&#128251; BLUETOOTH COMMANDS</h3>";
  html += "<div class='bt-badge'>U = Unlock &nbsp;|&nbsp; L = Lock &nbsp;|&nbsp; S = Status</div>";
  html += "<div class='bt-badge'>R = Reset Attempts &nbsp;|&nbsp; H = Help</div>";
  html += "<p style='font-size:.75em;color:#334e72;text-align:center;margin-top:10px'>Connect phone to: <b style='color:#00d2ff'>" + String(BT_DEVICE_NAME) + "</b></p>";
  html += "</div>";

  // Controls card
  html += "<div class='card'><h3>&#127917; REMOTE CONTROL (WiFi)</h3>";
  if (!permanentLock) {
    html += "<form action='/unlock' method='POST'><button class='bu'>&#128275; REMOTE UNLOCK</button></form>";
    html += "<form action='/lock'   method='POST'><button class='bl'>&#128274; REMOTE LOCK</button></form>";
  }
  html += "<form action='/reset' method='POST'><button class='br'>&#8635; RESET ATTEMPTS &amp; UNBLOCK</button></form>";
  html += "</div>";

  // Log card
  html += "<div class='card'><h3>&#128203; ENTRY LOG</h3>";
  if (totalLogs == 0) {
    html += "<p class='empty'>No events recorded yet.</p>";
  } else {
    for (int i = 0; i < totalLogs; i++) {
      int    idx = ((logIndex - 1 - i) + MAX_LOGS) % MAX_LOGS;
      String msg = String(logs[idx].message);
      String cls = "g";
      if (msg.indexOf("Wrong") >= 0 || msg.indexOf("BLOCKED") >= 0) cls = "w";
      else if (msg.indexOf("Bluetooth") >= 0) cls = "b";
      else if (msg.indexOf("remote") >= 0 || msg.indexOf("Remote") >= 0) cls = "b";
      else if (msg.indexOf("locked") >= 0) cls = "r";
      html += "<div class='log-row'>";
      html += "<span class='lt'>" + String(logs[idx].timestamp) + "</span>";
      html += "<span class='lm " + cls + "'>" + msg + "</span>";
      html += "</div>";
    }
  }
  html += "</div>";

  html += "<p class='foot'>Auto-refresh: 10s &nbsp;|&nbsp; " + WiFi.localIP().toString() + "</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleUnlock() {
  if (!permanentLock) remoteUnlockPending = true;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleLock() {
  remoteLockPending = true;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() {
  wrongAttempts = 0;
  permanentLock = false;
  addLog("Reset via web");
  ledLocked();                     // ✅ Reset LED on web reset (NEW)
  server.sendHeader("Location", "/");
  server.send(303);
}

// ─────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Relay - start LOCKED
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  // ✅ LED pins setup (NEW)
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED,   OUTPUT);

  // ✅ Startup LED test — blink both once (NEW)
  digitalWrite(LED_GREEN, HIGH); delay(300);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED,   HIGH); delay(300);
  digitalWrite(LED_RED,   LOW);
  delay(200);
  ledLocked(); // Start with Red ON (door is locked)

  // LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SMART DOOR LOCK ");
  lcd.setCursor(0, 1);
  lcd.print("Starting...     ");
  delay(1000);

  // Bluetooth
  SerialBT.begin(BT_DEVICE_NAME);
  Serial.println("Bluetooth started: " + String(BT_DEVICE_NAME));
  lcd.setCursor(0, 1);
  lcd.print("BT Ready!       ");
  delay(1000);

  // WiFi
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connecting.");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 24) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.println("\nWiFi Connected! IP: " + ip);
    syncTime();
    addLog("System started - IP: " + ip);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected! ");
    lcd.setCursor(0, 1);
    lcd.print(ip.substring(0, 16));
    delay(3000);

    btSend("=== SmartDoorLock Online ===");
    btSend("WiFi IP : " + ip);
    btSend("BT Name : " + String(BT_DEVICE_NAME));
    btSend("Send H for commands list");
    btSend("============================");
  } else {
    Serial.println("\nWiFi FAILED - offline mode");
    addLog("System started - offline");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi FAILED     ");
    lcd.setCursor(0, 1);
    lcd.print("BT Only Mode    ");
    delay(2000);
  }

  // Web server routes
  server.on("/",       HTTP_GET,  handleRoot);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.on("/lock",   HTTP_POST, handleLock);
  server.on("/reset",  HTTP_POST, handleReset);
  server.begin();
  Serial.println("Web server started.");

  lcd.clear();
}

// ─────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────
void loop() {
  server.handleClient();
  handleBluetooth();

  // Remote WiFi commands
  if (remoteUnlockPending && !permanentLock) {
    remoteUnlockPending = false;
    unlockDoor("WiFi web");
    return;
  }

  if (remoteLockPending) {
    remoteLockPending = false;
    if (!isDoorLocked) addLog("Locked - WiFi web");
    lockDoor();
    return;
  }

  // Permanent Lock
  if (permanentLock) {
    lcd.setCursor(0, 0);
    lcd.print("SYSTEM BLOCKED  ");
    lcd.setCursor(0, 1);
    lcd.print("NO ACCESS       ");
    digitalWrite(RELAY_PIN, HIGH);
    ledBlockedBlink();             // ✅ Fast red blink when blocked (NEW)
    return;
  }

  // Door is Open
  if (!isDoorLocked) {
    lcd.setCursor(0, 0);
    lcd.print("DOOR IS OPEN    ");
    lcd.setCursor(0, 1);
    lcd.print("Press A to Lock ");
    char key = keypad.getKey();
    if (key == 'A') lockDoor();
    return;
  }

  // Door is Locked - enter password
  lcd.setCursor(0, 0);
  lcd.print(" ENTER PASSWORD ");

  char key = keypad.getKey();
  if (key != NO_KEY) {
    delay(60);
    if      (key == 'C') resetPassword();
    else if (key == 'A') lockDoor();
    else if (key == 'D') checkPassword();
    else                 processKey(key);
  }
}
