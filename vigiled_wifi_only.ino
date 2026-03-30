/*
 * ═══════════════════════════════════════════════════════════
 *  XIAO ESP32C6 — Contrôleur LED NeoPixel  [ WiFi AP ]
 *
 *  BLE supprimé pour tenir dans la flash (107% → ~70%)
 *  Contrôle via WiFi uniquement :
 *    SSID     : VIGILED
 *    Mot passe: 12345678
 *    URL      : http://192.168.4.1
 *    Commandes: http://192.168.4.1/cmd?c=MODE:4
 *
 *  Bibliothèque requise : Adafruit NeoPixel
 *  (WiFi + WebServer sont inclus dans le core ESP32)
 * ═══════════════════════════════════════════════════════════
 */

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>

// ─── Broches ──────────────────────────────────────────────
#define PIN_NEOPIXEL   1
#define NUM_LEDS       28
#define LEDS_ACTIVE    9
#define BTN_MODE_PIN   2
#define BTN_BRIGHT_PIN 21

// ─── WiFi AP ──────────────────────────────────────────────
const char* AP_SSID = "VIGILED";
const char* AP_PASS = "12345678";
WebServer server(80);

// ─── Globals ──────────────────────────────────────────────
Adafruit_NeoPixel strip(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

uint8_t  currentMode = 0;
uint8_t  brightness  = 180;
bool     brightUp    = true;

uint8_t pastelR[4] = {  4,  35, 100, 180 };
uint8_t pastelG[4] = { 85,  80,  60,  40 };
uint8_t pastelB[4] = { 42, 160, 120,  20 };

// ─── Bouton MODE ──────────────────────────────────────────
uint8_t       etatBtnMode     = 0;
unsigned long tempsAppuiMode  = 0;
bool          longPressTraite = false;

// ─── Bouton BRIGHT ────────────────────────────────────────
uint8_t       etatBtnBright    = 0;
unsigned long tempsAppuiBright = 0;
unsigned long dernierAjustBri  = 0;

bool          flashActif   = false;
uint8_t       flashEtape   = 0;
unsigned long flashDernier = 0;
bool          flashEstMax  = true;

// ─── Parser de commandes ──────────────────────────────────
String parseCmd(String cmd) {
  cmd.trim();
  Serial.println("CMD: " + cmd);

  if (cmd.startsWith("MODE:")) {
    int m = cmd.substring(5).toInt();
    if (m >= 0 && m <= 13) { currentMode = m; return "OK:MODE:" + String(currentMode); }

  } else if (cmd.startsWith("BRI:")) {
    int b = cmd.substring(4).toInt();
    if (b >= 80 && b <= 255) { brightness = b; strip.setBrightness(brightness); }
    return "OK:BRI:" + String(brightness);

  } else if (cmd.startsWith("COL:")) {
    int p1 = cmd.indexOf(':', 4);
    int p2 = cmd.indexOf(':', p1 + 1);
    int p3 = cmd.indexOf(':', p2 + 1);
    if (p1 > 0 && p2 > 0 && p3 > 0) {
      int idx = cmd.substring(4, p1).toInt();
      if (idx >= 0 && idx <= 3) {
        pastelR[idx] = constrain(cmd.substring(p1+1, p2).toInt(), 0, 255);
        pastelG[idx] = constrain(cmd.substring(p2+1, p3).toInt(), 0, 255);
        pastelB[idx] = constrain(cmd.substring(p3+1).toInt(),     0, 255);
        return "OK:COL:" + String(idx);
      }
    }

  } else if (cmd == "OFF") {
    currentMode = 6; strip.clear(); strip.show(); return "OK:OFF";

  } else if (cmd == "RESET") {
    currentMode = 0; brightness = 180;
    pastelR[0]=4;   pastelG[0]=85;  pastelB[0]=42;
    pastelR[1]=35;  pastelG[1]=80;  pastelB[1]=160;
    pastelR[2]=100; pastelG[2]=60;  pastelB[2]=120;
    pastelR[3]=180; pastelG[3]=40;  pastelB[3]=20;
    strip.setBrightness(brightness); return "OK:RESET";

  } else if (cmd == "STATUS") {
    return "STATUS:MODE=" + String(currentMode)
         + ",BRI=" + String(brightness)
         + ",R0=" + String(pastelR[0]) + ",G0=" + String(pastelG[0]) + ",B0=" + String(pastelB[0])
         + ",R1=" + String(pastelR[1]) + ",G1=" + String(pastelG[1]) + ",B1=" + String(pastelB[1])
         + ",R2=" + String(pastelR[2]) + ",G2=" + String(pastelG[2]) + ",B2=" + String(pastelB[2])
         + ",R3=" + String(pastelR[3]) + ",G3=" + String(pastelG[3]) + ",B3=" + String(pastelB[3]);
  }
  return "ERR:UNKNOWN:" + cmd;
}

// ─── Routes HTTP ──────────────────────────────────────────
void handleCmd() {
  // CORS pour Safari iOS
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("c")) { server.send(400, "text/plain", "ERR:NO_CMD"); return; }
  String rep = parseCmd(server.arg("c"));
  server.send(200, "text/plain", rep);
}

void handleRoot() {
  // Redirection vers l'app externe, ou page minimaliste
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "VIGILED OK — utilisez /cmd?c=STATUS");
}

// ─── Effets LED ───────────────────────────────────────────
void respiration() {
  static unsigned long start = millis();
  unsigned long t = millis() - start;
  uint8_t bri = 10;
  if      (t < 2500)  bri = map(t, 0, 2500, 10, 255);
  else if (t < 8500)  bri = 255;
  else if (t < 11000) bri = map(t - 8500, 0, 2500, 255, 10);
  else { start = millis(); return; }
  uint8_t idx = (currentMode <= 3) ? currentMode : 0;
  uint8_t r = (uint32_t)pastelR[idx] * bri / 255 * brightness / 255;
  uint8_t g = (uint32_t)pastelG[idx] * bri / 255 * brightness / 255;
  uint8_t b = (uint32_t)pastelB[idx] * bri / 255 * brightness / 255;
  strip.fill(strip.Color(r, g, b), 0, LEDS_ACTIVE);
  strip.show();
}

void rainbow() {
  static uint16_t j = 0;
  for (uint8_t i = 0; i < LEDS_ACTIVE; i++)
    strip.setPixelColor(i, strip.ColorHSV((j + i * 8192UL) % 65536, 255, brightness));
  strip.show();
  j = (j + 256) % 65536;
  delay(20);
}

void drapeauFrancais() {
  static float phase = 0.0f;
  phase += 0.07f;
  if (phase > TWO_PI) phase -= TWO_PI;
  for (int i = 0; i < LEDS_ACTIVE; i++) {
    float pos  = (float)i / (LEDS_ACTIVE - 1);
    float wave = sinf(pos * PI * 3.5f + phase) * 0.35f + 0.65f;
    uint8_t w  = (uint8_t)(255 * wave);
    uint32_t col;
    if      (pos < 0.333f) col = strip.Color(0, 0, w);
    else if (pos < 0.666f) col = strip.Color(w, w, w);
    else                   col = strip.Color(w, 0, 0);
    strip.setPixelColor(i, col);
  }
  strip.show(); delay(30);
}

void vagueFluide() {
  static unsigned long t0 = millis();
  float t = (millis() - t0) / 1000.0f;
  for (int i = 0; i < LEDS_ACTIVE; i++) {
    float phase = ((float)i / (LEDS_ACTIVE-1)) * TWO_PI - t * 0.8f;
    float bri   = sinf(phase) * 0.4f + 0.6f;
    strip.setPixelColor(i, strip.Color(
      (uint8_t)(20  * bri * brightness / 255),
      (uint8_t)(80  * bri * brightness / 255),
      (uint8_t)(160 * bri * brightness / 255)
    ));
  }
  strip.show(); delay(20);
}

void policeMode() {
  static uint8_t       step = 0;
  static unsigned long last = 0;
  if (millis() - last < 50) return;
  last = millis();
  strip.clear();
  if (step < 8)       { if (step % 2 == 0) strip.fill(0xFF0000); }
  else if (step < 16) { if ((step-8) % 2 == 0) strip.fill(0x0000FF); }
  strip.show();
  if (++step >= 18) { step = 0; delay(380); }
}

void veilleDiscrete() {
  static unsigned long t0 = millis();
  float t   = (float)((millis() - t0) % 4000) / 4000.0f;
  float bri = (sinf(t * TWO_PI - HALF_PI) + 1.0f) / 2.0f;
  float sc  = bri * 0.35f + 0.02f;
  strip.clear();
  int mid = LEDS_ACTIVE / 2;
  uint8_t r = (uint8_t)(40  * sc * brightness / 255);
  uint8_t g = (uint8_t)(130 * sc * brightness / 255);
  uint8_t b = (uint8_t)(80  * sc * brightness / 255);
  strip.setPixelColor(mid, strip.Color(r, g, b));
  if (mid > 0)             strip.setPixelColor(mid-1, strip.Color(r/4, g/4, b/4));
  if (mid < LEDS_ACTIVE-1) strip.setPixelColor(mid+1, strip.Color(r/4, g/4, b/4));
  strip.show(); delay(20);
}

void fixeStable() {
  uint8_t r = (uint32_t)pastelR[0] * brightness / 255;
  uint8_t g = (uint32_t)pastelG[0] * brightness / 255;
  uint8_t b = (uint32_t)pastelB[0] * brightness / 255;
  strip.fill(strip.Color(r, g, b), 0, LEDS_ACTIVE);
  strip.show(); delay(100);
}

void k2000Rouge() {
  static int           pos  = 0;
  static int           dir  = 1;
  static unsigned long last = 0;
  if (millis() - last < 55) return;
  last = millis();
  strip.clear();
  for (int i = 0; i < LEDS_ACTIVE; i++) {
    int     dist = abs(i - pos);
    uint8_t r    = (dist==0) ? 255 : (dist==1) ? 100 : (dist==2) ? 30 : 0;
    strip.setPixelColor(i, strip.Color(r * brightness / 255, 0, 0));
  }
  strip.show();
  pos += dir;
  if (pos >= LEDS_ACTIVE-1) { pos = LEDS_ACTIVE-1; dir = -1; }
  if (pos <= 0)              { pos = 0;              dir =  1; }
}

// ─── Bouton MODE ──────────────────────────────────────────
void gererBoutonMode() {
  bool btn = (digitalRead(BTN_MODE_PIN) == LOW);

  if (btn && etatBtnMode == 0) {
    etatBtnMode = 1; tempsAppuiMode = millis(); longPressTraite = false;
  }
  if (btn && etatBtnMode == 1 && !longPressTraite && millis() - tempsAppuiMode > 2000) {
    longPressTraite = true; etatBtnMode = 2;
    currentMode = (currentMode == 10) ? 0 : 10;
  }
  if (!btn && etatBtnMode == 1) {
    if (millis() - tempsAppuiMode < 2000 && currentMode != 10) {
      const uint8_t cycle[] = { 0,1,2,3,4,5,7,11,12,13,6 };
      const uint8_t len = sizeof(cycle);
      uint8_t idx = 0;
      for (uint8_t i = 0; i < len; i++) { if (cycle[i] == currentMode) { idx = i; break; } }
      currentMode = cycle[(idx + 1) % len];
    }
    etatBtnMode = 0;
  }
  if (!btn && etatBtnMode == 2) etatBtnMode = 0;
}

// ─── Bouton BRIGHT ────────────────────────────────────────
void declencherFlash(bool estMax) {
  flashActif = true; flashEtape = 0; flashDernier = millis(); flashEstMax = estMax;
}

void gererBoutonBright() {
  bool btn = (digitalRead(BTN_BRIGHT_PIN) == LOW);
  if (btn && etatBtnBright == 0) {
    etatBtnBright = 1; tempsAppuiBright = millis(); dernierAjustBri = millis();
  }
  if (btn && etatBtnBright == 1 && !flashActif) {
    unsigned long now = millis();
    if (now - tempsAppuiBright > 400 && now - dernierAjustBri > 80) {
      dernierAjustBri = now;
      if (brightUp) {
        if (brightness < 255) { brightness = min(255, brightness + 5); strip.setBrightness(brightness); }
        if (brightness >= 255 && !flashActif) declencherFlash(true);
      } else {
        if (brightness > 25)  { brightness = max(25,  brightness - 5); strip.setBrightness(brightness); }
        if (brightness <= 25  && !flashActif) declencherFlash(false);
      }
    }
  }
  if (!btn && etatBtnBright == 1) {
    if (millis() - tempsAppuiBright < 400) brightUp = !brightUp;
    etatBtnBright = 0;
  }
}

// ─── Flash limite luminosité ──────────────────────────────
void gererFlashLimite() {
  if (!flashActif || millis() - flashDernier < 100) return;
  flashDernier = millis();
  if (flashEtape % 2 == 0) {
    strip.setBrightness(255);
    strip.fill(flashEstMax ? strip.Color(255,255,255) : strip.Color(200,30,30), 0, LEDS_ACTIVE);
    strip.show();
  } else {
    strip.clear(); strip.show();
  }
  if (++flashEtape >= 6) {
    flashActif = false; flashEtape = 0;
    dernierAjustBri = millis();
    brightUp = !brightUp;
    strip.setBrightness(brightness);
  }
}

// ─── SETUP ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BTN_MODE_PIN,   INPUT_PULLUP);
  pinMode(BTN_BRIGHT_PIN, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();

  // WiFi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("AP : " + String(AP_SSID) + " — IP : " + WiFi.softAPIP().toString());

  // Routes HTTP
  server.on("/",    handleRoot);
  server.on("/cmd", handleCmd);
  server.begin();
  Serial.println("Serveur HTTP démarré sur port 80");
}

// ─── LOOP ─────────────────────────────────────────────────
void loop() {
  server.handleClient();

  gererBoutonMode();
  gererBoutonBright();
  gererFlashLimite();
  if (flashActif) return;

  if      (currentMode <= 3)  respiration();
  else if (currentMode == 4)  rainbow();
  else if (currentMode == 5)  drapeauFrancais();
  else if (currentMode == 6)  { strip.clear(); strip.show(); }
  else if (currentMode == 7)  vagueFluide();
  else if (currentMode == 10) policeMode();
  else if (currentMode == 11) veilleDiscrete();
  else if (currentMode == 12) fixeStable();
  else if (currentMode == 13) k2000Rouge();
}
