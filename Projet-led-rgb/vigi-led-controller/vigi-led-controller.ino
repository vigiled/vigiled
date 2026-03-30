#include <Adafruit_NeoPixel.h>

/*
 * ═══════════════════════════════════════════════════════════
 *  XIAO ESP32C6 — Contrôleur LED NeoPixel
 *  BLE (Android/Chrome) + WiFi WebSocket (iOS/Safari)
 *  Configuration WiFi via portail captif (WiFiManager)
 * ═══════════════════════════════════════════════════════════
 *
 *  Bibliothèques requises (Gestionnaire de bibliothèques Arduino) :
 *    - Adafruit NeoPixel
 *    - ESP32 Arduino core (Expressif)
 *    - WebSockets          → chercher "WebSockets" par Links2004
 *    - WiFiManager         → chercher "WiFiManager" par tzapu
 *
 *  ── Première utilisation ────────────────────────────────
 *  1. Flasher ce code
 *  2. L'ESP32 crée un réseau WiFi "LED-Setup"
 *  3. Connectez votre iPhone → Réglages → WiFi → LED-Setup
 *     Mot de passe : 12345678
 *  4. Safari ouvre automatiquement le portail (sinon : 192.168.4.1)
 *  5. Choisissez votre réseau WiFi, entrez le mot de passe
 *  6. L'ESP32 redémarre → son IP s'affiche dans le moniteur série
 *  7. Entrez cette IP dans la page web onglet WiFi
 *
 *  ── Réinitialiser le WiFi ───────────────────────────────
 *  Maintenir le bouton MODE (D2) appuyé 5s au démarrage
 *
 *  Câblage :
 *    LEDs NeoPixel  → D1 (GPIO1)
 *    Bouton MODE    → D2 (GPIO2) + GND
 *    Bouton BRIGHT  → D3 (GPIO3) + GND
 *    Batterie ADC   → A0 (GPIO0)
 *
 *  Commandes BLE / WebSocket :
 *    MODE:n     → mode 0-7, 10-13
 *    BRI:nnn    → luminosité 80-255
 *    COL:n:r:g:b → couleur pastel n (0-3)
 *    OFF        → éteindre
 *    RESET      → valeurs par défaut
 *    STATUS     → état actuel
 * ═══════════════════════════════════════════════════════════
 */

#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsServer.h>

// ─── Broches ──────────────────────────────────────────────
#define PIN_NEOPIXEL   1
#define NUM_LEDS       28
#define LEDS_ACTIVE    9
#define BTN_MODE_PIN   2
#define BTN_BRIGHT_PIN 3
#define BAT_PIN        A0

// ─── BLE NUS (Nordic UART Service) ────────────────────────
#define SERVICE_UUID   "6E400001-B5BA-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX   "6E400002-B5BA-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX   "6E400003-B5BA-F393-E0A9-E50E24DCCA9E"

// ─── Objets globaux ───────────────────────────────────────
Adafruit_NeoPixel strip(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

WebSocketsServer  wsServer(81);
uint8_t wsClientNum = 255;

BLEServer*         pServer  = nullptr;
BLECharacteristic* pTxChar  = nullptr;
bool bleConnected = false;
bool oldConnected = false;

// ─── État LED ─────────────────────────────────────────────
uint8_t currentMode = 0;
uint8_t brightness  = 180;
bool    brightUp    = true;

// Couleurs pastel modifiables
uint8_t pastelR[4] = {  4,  35, 100, 180 };
uint8_t pastelG[4] = { 85,  80,  60,  40 };
uint8_t pastelB[4] = { 42, 160, 120,  20 };

// ─── Commandes en attente ─────────────────────────────────
bool   cmdPending = false;
String pendingCmd = "";

// ─── Boutons ──────────────────────────────────────────────
unsigned long tempsAppuiMode   = 0;
unsigned long tempsAppuiBright = 0;
bool boutonModeAppuye   = false;
bool boutonBrightAppuye = false;

// ══════════════════════════════════════════════════════════
//  ENVOI UNIFIÉ BLE + WEBSOCKET
// ══════════════════════════════════════════════════════════
void sendResponse(String msg) {
  if (pTxChar && bleConnected) {
    pTxChar->setValue(msg.c_str());
    pTxChar->notify();
  }
  if (wsClientNum != 255) {
    wsServer.sendTXT(wsClientNum, msg);
  }
  Serial.println("OUT: " + msg);
}

// ══════════════════════════════════════════════════════════
//  WEBSOCKET EVENTS
// ══════════════════════════════════════════════════════════
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    wsClientNum = num;
    Serial.printf("WS client #%u connecté\n", num);
    // Envoyer le statut immédiatement à la connexion
    sendResponse("STATUS:MODE=" + String(currentMode) + ",BRI=" + String(brightness));
  } else if (type == WStype_DISCONNECTED) {
    wsClientNum = 255;
    Serial.printf("WS client #%u déconnecté\n", num);
  } else if (type == WStype_TEXT) {
    pendingCmd = String((char*)payload);
    cmdPending = true;
  }
}

// ══════════════════════════════════════════════════════════
//  BLE CALLBACKS
// ══════════════════════════════════════════════════════════
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    bleConnected = true;
    Serial.println("BLE: client connecté");
  }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("BLE: client déconnecté");
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String val = c->getValue().c_str();
    val.trim();
    if (val.length() > 0) {
      pendingCmd = val;
      cmdPending = true;
    }
  }
};

// ══════════════════════════════════════════════════════════
//  PARSER DE COMMANDES
// ══════════════════════════════════════════════════════════
void parseCmd(String cmd) {
  cmd.trim();
  Serial.println("CMD: " + cmd);

  if (cmd.startsWith("MODE:")) {
    int m = cmd.substring(5).toInt();
    currentMode = m;
    sendResponse("OK:MODE:" + String(currentMode));

  } else if (cmd.startsWith("BRI:")) {
    int b = cmd.substring(4).toInt();
    if (b >= 80 && b <= 255) {
      brightness = b;
      strip.setBrightness(brightness);
    }
    sendResponse("OK:BRI:" + String(brightness));

  } else if (cmd.startsWith("COL:")) {
    // Format: COL:n:r:g:b
    int p1 = cmd.indexOf(':', 4);
    int p2 = cmd.indexOf(':', p1 + 1);
    int p3 = cmd.indexOf(':', p2 + 1);
    if (p1 > 0 && p2 > 0 && p3 > 0) {
      int idx = cmd.substring(4,    p1).toInt();
      int r   = cmd.substring(p1+1, p2).toInt();
      int g   = cmd.substring(p2+1, p3).toInt();
      int b   = cmd.substring(p3+1).toInt();
      if (idx >= 0 && idx <= 3) {
        pastelR[idx] = constrain(r, 0, 255);
        pastelG[idx] = constrain(g, 0, 255);
        pastelB[idx] = constrain(b, 0, 255);
        sendResponse("OK:COL:" + String(idx));
      }
    }

  } else if (cmd == "OFF") {
    currentMode = 6;
    strip.clear(); strip.show();
    sendResponse("OK:OFF");

  } else if (cmd == "RESET") {
    currentMode = 0;
    brightness  = 180;
    pastelR[0] =   4; pastelG[0] = 85;  pastelB[0] = 42;
    pastelR[1] =  35; pastelG[1] = 80;  pastelB[1] = 160;
    pastelR[2] = 100; pastelG[2] = 60;  pastelB[2] = 120;
    pastelR[3] = 180; pastelG[3] = 40;  pastelB[3] = 20;
    strip.setBrightness(brightness);
    sendResponse("OK:RESET");

  } else if (cmd == "STATUS") {
    String s = "STATUS:MODE=" + String(currentMode)
             + ",BRI=" + String(brightness)
             + ",R0=" + String(pastelR[0]) + ",G0=" + String(pastelG[0]) + ",B0=" + String(pastelB[0])
             + ",R1=" + String(pastelR[1]) + ",G1=" + String(pastelG[1]) + ",B1=" + String(pastelB[1])
             + ",R2=" + String(pastelR[2]) + ",G2=" + String(pastelG[2]) + ",B2=" + String(pastelB[2])
             + ",R3=" + String(pastelR[3]) + ",G3=" + String(pastelG[3]) + ",B3=" + String(pastelB[3]);
    sendResponse(s);

  } else {
    sendResponse("ERR:UNKNOWN:" + cmd);
  }
}

// ══════════════════════════════════════════════════════════
//  BATTERIE
// ══════════════════════════════════════════════════════════
bool batterieCritique() {
  long sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogRead(BAT_PIN); delay(1); }
  return (sum >> 4) < 325;
}

void alerteEtExtinction() {
  for (int i = 0; i < 6; i++) {
    strip.fill(0xFF0000); strip.show(); delay(250);
    strip.clear();        strip.show(); delay(250);
  }
  esp_deep_sleep_start();
}

// ══════════════════════════════════════════════════════════
//  EFFETS LED
// ══════════════════════════════════════════════════════════

void feedbackLimite(bool estMax) {
  uint32_t c = estMax ? 0x00FF00 : 0x0000FF;
  for (int i = 0; i < 3; i++) {
    strip.fill(c, 0, LEDS_ACTIVE); strip.show(); delay(60);
    strip.clear(); strip.show(); delay(40);
  }
}

// Mode 0-3 : Respiration couleur pastel
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

// Mode 4 : Arc-en-ciel
void rainbow() {
  static uint16_t j = 0;
  for (uint8_t i = 0; i < LEDS_ACTIVE; i++) {
    strip.setPixelColor(i, strip.ColorHSV((j + i * 8192UL) % 65536, 255, brightness));
  }
  strip.show();
  j = (j + 256) % 65536;
  delay(20);
}

// Mode 5 : Drapeau français
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

// Mode 7 : Vague douce bleue
void vagueFluide() {
  static unsigned long t0 = millis();
  float t = (millis() - t0) / 1000.0f;
  for (int i = 0; i < LEDS_ACTIVE; i++) {
    float phase = ((float)i / (LEDS_ACTIVE - 1)) * TWO_PI - t * 0.8f;
    float bri   = sinf(phase) * 0.4f + 0.6f;
    uint8_t r = (uint8_t)(20  * bri * brightness / 255);
    uint8_t g = (uint8_t)(80  * bri * brightness / 255);
    uint8_t b = (uint8_t)(160 * bri * brightness / 255);
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show(); delay(20);
}

// Mode 10 : Police (urgence)
void policeMode() {
  static uint8_t step = 0;
  static unsigned long last = 0;
  if (millis() - last < 50) return;
  last = millis();
  strip.clear();
  if (step < 8)       { if (step % 2 == 0) strip.fill(0xFF0000); }
  else if (step < 16) { if ((step - 8) % 2 == 0) strip.fill(0x0000FF); }
  strip.show();
  if (++step >= 18) { step = 0; delay(380); }
}

// Mode 11 : Veille pulsée (LED centrale seule)
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
  if (mid > 0)             strip.setPixelColor(mid - 1, strip.Color(r/4, g/4, b/4));
  if (mid < LEDS_ACTIVE-1) strip.setPixelColor(mid + 1, strip.Color(r/4, g/4, b/4));
  strip.show(); delay(20);
}

// Mode 12 : Couleur fixe stable
void fixeStable() {
  uint8_t r = (uint32_t)pastelR[0] * brightness / 255;
  uint8_t g = (uint32_t)pastelG[0] * brightness / 255;
  uint8_t b = (uint32_t)pastelB[0] * brightness / 255;
  strip.fill(strip.Color(r, g, b), 0, LEDS_ACTIVE);
  strip.show(); delay(100);
}

// Mode 13 : K-2000 rouge avec traînée
void k2000Rouge() {
  static int  pos = 0;
  static int  dir = 1;
  static unsigned long last = 0;
  if (millis() - last < 55) return;
  last = millis();
  strip.clear();
  for (int i = 0; i < LEDS_ACTIVE; i++) {
    int dist = abs(i - pos);
    uint8_t r = 0;
    if      (dist == 0) r = 255;
    else if (dist == 1) r = 100;
    else if (dist == 2) r = 30;
    strip.setPixelColor(i, strip.Color(r * brightness / 255, 0, 0));
  }
  strip.show();
  pos += dir;
  if (pos >= LEDS_ACTIVE - 1) { pos = LEDS_ACTIVE - 1; dir = -1; }
  if (pos <= 0)                { pos = 0;                dir =  1; }
}

// ══════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n═══ VIGI-LED démarrage ═══");

  pinMode(BTN_MODE_PIN,   INPUT_PULLUP);
  pinMode(BTN_BRIGHT_PIN, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();

  // ── Réinitialisation WiFi si bouton MODE tenu au boot ──
  if (digitalRead(BTN_MODE_PIN) == LOW) {
    Serial.println("Bouton MODE tenu — reset WiFi dans 3s...");
    strip.fill(strip.Color(80, 0, 0), 0, LEDS_ACTIVE); strip.show();
    delay(3000);
    if (digitalRead(BTN_MODE_PIN) == LOW) {
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("✓ Credentials WiFi effacés !");
      strip.fill(strip.Color(0, 80, 0), 0, LEDS_ACTIVE); strip.show();
      delay(1000);
      ESP.restart();
    }
  }

  // ── Pulsation orange pendant la config WiFi ────────────
  // (utilisée comme callback dans le portail)
  auto ledPortalCallback = [](WiFiManager* wm) {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("Portail de configuration actif !");
    Serial.println("Connectez-vous au WiFi : LED-Setup");
    Serial.println("Mot de passe           : 12345678");
    Serial.println("Puis ouvrez            : http://192.168.4.1");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  };

  // ── WiFiManager ────────────────────────────────────────
  WiFiManager wm;
  wm.setAPName("LED-Setup");
  wm.setConfigPortalTimeout(180);   // 3 minutes puis reboot
  wm.setAPCallback(ledPortalCallback);

  // Pulsation orange pendant que le portail est actif
  wm.setWebServerCallback([]() {
    static unsigned long last = 0;
    static float phase = 0;
    if (millis() - last > 20) {
      last = millis();
      phase += 0.08f;
      float bri = (sinf(phase) + 1.0f) / 2.0f;
      uint8_t v = (uint8_t)(bri * 100);
      strip.fill(strip.Color(v, v / 3, 0), 0, LEDS_ACTIVE);
      strip.show();
    }
  });

  bool connected = wm.autoConnect("LED-Setup", "12345678");

  if (!connected) {
    Serial.println("Timeout portail — redémarrage...");
    strip.fill(strip.Color(80, 0, 0), 0, LEDS_ACTIVE); strip.show();
    delay(1000);
    ESP.restart();
  }

  // ── WiFi connecté ! ────────────────────────────────────
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("✓ WiFi connecté !");
  Serial.println("IP : " + WiFi.localIP().toString());
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

  // Clignotement vert = WiFi OK
  for (int i = 0; i < 3; i++) {
    strip.fill(strip.Color(0, 100, 30), 0, LEDS_ACTIVE); strip.show(); delay(200);
    strip.clear(); strip.show(); delay(150);
  }

  // Démarrer WebSocket sur port 81
  wsServer.begin();
  wsServer.onEvent(onWsEvent);
  Serial.println("✓ WebSocket démarré — port 81");

  // ── BLE ────────────────────────────────────────────────
  BLEDevice::init("LED-Controller");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic* pRxChar = pService->createCharacteristic(
    CHAR_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxChar->setCallbacks(new RxCallbacks());

  pTxChar = pService->createCharacteristic(
    CHAR_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxChar->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("✓ BLE démarré — LED-Controller");
  Serial.println("═══ Prêt ! ═══\n");
}

// ══════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════
void loop() {
  // WebSocket
  wsServer.loop();

  // Reconnexion BLE automatique après déconnexion
  if (!bleConnected && oldConnected) {
    delay(300);
    pServer->startAdvertising();
    oldConnected = false;
  }
  if (bleConnected && !oldConnected) {
    oldConnected = true;
  }

  // Traiter commande entrante (BLE ou WebSocket)
  if (cmdPending) {
    cmdPending = false;
    parseCmd(pendingCmd);
  }

  // Alerte batterie
  if (batterieCritique()) alerteEtExtinction();

  // ── Boutons physiques ──────────────────────────────────
  bool btnMode   = digitalRead(BTN_MODE_PIN);
  bool btnBright = digitalRead(BTN_BRIGHT_PIN);

  // Bouton MODE
  if (btnMode == LOW && !boutonModeAppuye) {
    boutonModeAppuye = true;
    tempsAppuiMode = millis();
  }
  if (btnMode == LOW && boutonModeAppuye) {
    if (millis() - tempsAppuiMode > 2000) {
      currentMode = (currentMode == 10) ? 0 : 10;
      while (digitalRead(BTN_MODE_PIN) == LOW) delay(10);
      boutonModeAppuye = false;
    }
  }
  if (btnMode == HIGH && boutonModeAppuye) {
    if (millis() - tempsAppuiMode < 1800 && currentMode < 10)
      currentMode = (currentMode + 1) % 7;
    boutonModeAppuye = false;
  }

  // Bouton BRIGHTNESS
  if (btnBright == LOW && !boutonBrightAppuye) {
    boutonBrightAppuye = true;
    tempsAppuiBright = millis();
  }
  if (btnBright == LOW && boutonBrightAppuye) {
    static unsigned long lastAdj = 0;
    if (millis() - tempsAppuiBright > 400 && millis() - lastAdj > 65) {
      lastAdj = millis();
      bool limite = false;
      if (brightUp) {
        if (brightness <= 248) brightness += 7;
        else { brightness = 255; limite = true; }
      } else {
        if (brightness >= 87)  brightness -= 7;
        else { brightness = 80;  limite = true; }
      }
      strip.setBrightness(brightness);
      if (limite) feedbackLimite(brightUp);
    }
  }
  if (btnBright == HIGH && boutonBrightAppuye) {
    if (millis() - tempsAppuiBright > 500) brightUp = !brightUp;
    boutonBrightAppuye = false;
  }

  // ── Rendu LED selon le mode actif ─────────────────────
  if      (currentMode <= 3)  respiration();
  else if (currentMode == 4)  rainbow();
  else if (currentMode == 5)  drapeauFrancais();
  else if (currentMode == 6)  { strip.clear(); strip.show(); delay(50); }
  else if (currentMode == 7)  vagueFluide();
  else if (currentMode == 10) policeMode();
  else if (currentMode == 11) veilleDiscrete();
  else if (currentMode == 12) fixeStable();
  else if (currentMode == 13) k2000Rouge();
}
