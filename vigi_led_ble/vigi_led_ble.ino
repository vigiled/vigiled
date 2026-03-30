/*
 * ═══════════════════════════════════════════════════════════
 *  XIAO ESP32C6 — Contrôleur LED NeoPixel BLE
 *  Corrections :
 *    - Bouton luminosité : suppression du feedbackLimite()
 *      qui bloquait la détection du relâchement
 *    - Mode police appui long : suppression du while() bloquant,
 *      remplacement par machine à états non-bloquante
 * ═══════════════════════════════════════════════════════════
 */

#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ─── Broches ──────────────────────────────────────────────
#define PIN_NEOPIXEL   1       // D1 sur XIAO ESP32C6
#define NUM_LEDS       28
#define LEDS_ACTIVE    9

#define BTN_MODE_PIN   2       // D2
#define BTN_BRIGHT_PIN 3       // D3

// ─── BLE NUS (Nordic UART Service) ────────────────────────
#define SERVICE_UUID   "6E400001-B5BA-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX   "6E400002-B5BA-F393-E0A9-E50E24DCCA9E"  // write (web → esp)
#define CHAR_UUID_TX   "6E400003-B5BA-F393-E0A9-E50E24DCCA9E"  // notify (esp → web)

// ─── Globals ──────────────────────────────────────────────
Adafruit_NeoPixel strip(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

BLEServer*         pServer      = nullptr;
BLECharacteristic* pTxChar      = nullptr;
bool               bleConnected = false;
bool               oldConnected = false;

uint8_t  currentMode = 0;
uint8_t  brightness  = 180;
bool     brightUp    = true;

// Couleurs pastel modifiables
uint8_t pastelR[4] = {  4,  35, 100, 180 };
uint8_t pastelG[4] = { 85,  80,  60,  40 };
uint8_t pastelB[4] = { 42, 160, 120,  20 };

// ─── Gestion bouton MODE (machine à états) ────────────────
// États : 0=relâché, 1=appui détecté, 2=appui long confirmé
uint8_t       etatBtnMode      = 0;
unsigned long tempsAppuiMode   = 0;
bool          longPressTraite  = false;

// ─── Gestion bouton BRIGHT (machine à états) ──────────────
uint8_t       etatBtnBright    = 0;
unsigned long tempsAppuiBright = 0;
unsigned long dernierAjustBri  = 0;

// ─── BLE buffer ───────────────────────────────────────────
bool   cmdPending = false;
String pendingCmd = "";

// ─── BLE Callbacks ────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    bleConnected = true;
    Serial.println("BLE: client connecte");
  }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("BLE: client deconnecte");
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

// ─── BLE Send ─────────────────────────────────────────────
void bleSend(String msg) {
  if (pTxChar && bleConnected) {
    pTxChar->setValue(msg.c_str());
    pTxChar->notify();
  }
  Serial.println("BLE OUT: " + msg);
}

// ─── Parser de commandes ──────────────────────────────────
void parseCmd(String cmd) {
  cmd.trim();
  Serial.println("CMD: " + cmd);

  if (cmd.startsWith("MODE:")) {
    int m = cmd.substring(5).toInt();
    if (m >= 0 && m <= 13) {
      currentMode = m;
      bleSend("OK:MODE:" + String(currentMode));
    }

  } else if (cmd.startsWith("BRI:")) {
    int b = cmd.substring(4).toInt();
    if (b >= 80 && b <= 255) {
      brightness = b;
      strip.setBrightness(brightness);
      bleSend("OK:BRI:" + String(brightness));
    }

  } else if (cmd.startsWith("COL:")) {
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
        bleSend("OK:COL:" + String(idx));
      }
    }

  } else if (cmd == "OFF") {
    currentMode = 6;
    strip.clear(); strip.show();
    bleSend("OK:OFF");

  } else if (cmd == "RESET") {
    currentMode = 0;
    brightness  = 180;
    pastelR[0] =   4; pastelG[0] = 85;  pastelB[0] = 42;
    pastelR[1] =  35; pastelG[1] = 80;  pastelB[1] = 160;
    pastelR[2] = 100; pastelG[2] = 60;  pastelB[2] = 120;
    pastelR[3] = 180; pastelG[3] = 40;  pastelB[3] = 20;
    strip.setBrightness(brightness);
    bleSend("OK:RESET");

  } else if (cmd == "STATUS") {
    String s = "STATUS:MODE=" + String(currentMode)
             + ",BRI="  + String(brightness)
             + ",R0=" + String(pastelR[0]) + ",G0=" + String(pastelG[0]) + ",B0=" + String(pastelB[0])
             + ",R1=" + String(pastelR[1]) + ",G1=" + String(pastelG[1]) + ",B1=" + String(pastelB[1])
             + ",R2=" + String(pastelR[2]) + ",G2=" + String(pastelG[2]) + ",B2=" + String(pastelB[2])
             + ",R3=" + String(pastelR[3]) + ",G3=" + String(pastelG[3]) + ",B3=" + String(pastelB[3]);
    bleSend(s);

  } else {
    bleSend("ERR:UNKNOWN:" + cmd);
  }
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

// ─── Gestion bouton MODE (non-bloquante) ──────────────────
/*
 * Appui court (<2s) : mode suivant (cycle 0→6)
 * Appui long  (>2s) : bascule mode police (10) / retour mode précédent
 *
 * CORRECTION : suppression du while() bloquant remplacé par
 * un flag longPressTraite pour n'agir qu'une seule fois sur
 * l'appui long, sans jamais bloquer la loop principale.
 */
void gererBoutonMode() {
  bool btn = (digitalRead(BTN_MODE_PIN) == LOW);

  if (btn && etatBtnMode == 0) {
    // Front descendant : début d'appui
    etatBtnMode     = 1;
    tempsAppuiMode  = millis();
    longPressTraite = false;
  }

  if (btn && etatBtnMode == 1) {
    // Bouton maintenu — vérifie appui long
    if (!longPressTraite && millis() - tempsAppuiMode > 2000) {
      longPressTraite = true;
      etatBtnMode     = 2; // appui long confirmé
      currentMode = (currentMode == 10) ? 0 : 10;
      Serial.println("Appui long MODE → " + String(currentMode));
    }
  }

  if (!btn && etatBtnMode == 1) {
    // Relâchement après appui court
    unsigned long duree = millis() - tempsAppuiMode;
    if (duree < 2000 && currentMode != 10) {
      // Cycle modes normaux 0→6, saute le mode police
      currentMode = (currentMode + 1) % 7;
      Serial.println("Appui court MODE → " + String(currentMode));
    }
    etatBtnMode = 0;
  }

  if (!btn && etatBtnMode == 2) {
    // Relâchement après appui long — rien à faire, déjà traité
    etatBtnMode = 0;
  }
}

// ─── Gestion bouton BRIGHT (non-bloquante) ────────────────
/*
 * Appui court  : inverse la direction (monter/descendre)
 * Appui long   : ajustement continu de la luminosité
 *
 * CORRECTION : suppression du feedbackLimite() qui appelait
 * des delay() pendant que le bouton était encore enfoncé,
 * ce qui faisait rater la détection du relâchement et
 * bloquait le rendu LED.
 */
void gererBoutonBright() {
  bool btn = (digitalRead(BTN_BRIGHT_PIN) == LOW);

  if (btn && etatBtnBright == 0) {
    etatBtnBright    = 1;
    tempsAppuiBright = millis();
  }

  if (btn && etatBtnBright == 1) {
    // Ajustement continu après 400ms d'appui
    if (millis() - tempsAppuiBright > 400 &&
        millis() - dernierAjustBri  > 65) {
      dernierAjustBri = millis();
      if (brightUp) {
        if (brightness <= 248) brightness += 7;
        else                   brightness  = 255;
      } else {
        if (brightness >= 87)  brightness -= 7;
        else                   brightness  = 80;
      }
      strip.setBrightness(brightness);
      Serial.println("BRI=" + String(brightness));
    }
  }

  if (!btn && etatBtnBright == 1) {
    // Relâchement : si appui court → inverse direction
    if (millis() - tempsAppuiBright < 400) {
      brightUp = !brightUp;
      Serial.println("Direction bri: " + String(brightUp ? "+" : "-"));
    }
    etatBtnBright = 0;
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

  Serial.println("BLE demarre: LED-Controller");
}

// ─── LOOP ─────────────────────────────────────────────────
void loop() {

  // DEBUG temporaire — affiche l'état des boutons toutes les 300ms
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 300) {
    lastDebug = millis();
    Serial.println("BTN_MODE(GPIO2)=" + String(digitalRead(2)) +
                   "  BTN_BRIGHT(GPIO21)=" + String(digitalRead(21)) +
                   "  BTN_BRIGHT(GPIO3)=" + String(digitalRead(3)));
  }

  // Reconnexion BLE auto
  if (!bleConnected && oldConnected) {
    delay(300);
    pServer->startAdvertising();
    oldConnected = false;
  }
  if (bleConnected && !oldConnected) {
    oldConnected = true;
  }

  // Commande BLE entrante
  if (cmdPending) {
    cmdPending = false;
    parseCmd(pendingCmd);
  }

  // Boutons physiques
  gererBoutonMode();
  gererBoutonBright();

  // Rendu LED
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
