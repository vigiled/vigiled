/*
 * ═══════════════════════════════════════════════════════════
 *  XIAO ESP32C6 — Contrôleur LED NeoPixel BLE + WiFi AP
 *
 *  MODE WiFi :
 *    - Point d'accès : SSID = VIGILED, mot de passe = 12345678
 *    - Interface web sur http://192.168.4.1
 *    - Compatible iOS/Android/PC via navigateur (Safari, Chrome…)
 *    - BLE reste actif en parallèle
 * ═══════════════════════════════════════════════════════════
 */

#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
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

// ─── BLE NUS ──────────────────────────────────────────────
#define SERVICE_UUID   "6E400001-B5BA-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX   "6E400002-B5BA-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX   "6E400003-B5BA-F393-E0A9-E50E24DCCA9E"

// ─── Globals ──────────────────────────────────────────────
Adafruit_NeoPixel strip(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

BLEServer*         pServer      = nullptr;
BLECharacteristic* pTxChar      = nullptr;
bool               bleConnected = false;
bool               oldConnected = false;

uint8_t  currentMode = 0;
uint8_t  brightness  = 180;
bool     brightUp    = true;

uint8_t pastelR[4] = {  4,  35, 100, 180 };
uint8_t pastelG[4] = { 85,  80,  60,  40 };
uint8_t pastelB[4] = { 42, 160, 120,  20 };

// ─── Bouton MODE ──────────────────────────────────────────
uint8_t       etatBtnMode      = 0;
unsigned long tempsAppuiMode   = 0;
bool          longPressTraite  = false;

// ─── Bouton BRIGHT ────────────────────────────────────────
uint8_t       etatBtnBright    = 0;
unsigned long tempsAppuiBright = 0;
unsigned long dernierAjustBri  = 0;

bool          flashActif   = false;
uint8_t       flashEtape   = 0;
unsigned long flashDernier = 0;
bool          flashEstMax  = true;

// ─── BLE buffer ───────────────────────────────────────────
bool   cmdPending = false;
String pendingCmd = "";

// ─── BLE Callbacks ────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override    { bleConnected = true;  Serial.println("BLE: connecte"); }
  void onDisconnect(BLEServer* s) override { bleConnected = false; Serial.println("BLE: deconnecte"); }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String val = c->getValue().c_str();
    val.trim();
    if (val.length() > 0) { pendingCmd = val; cmdPending = true; }
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

// ─── Parser de commandes (partagé BLE + WiFi) ─────────────
void parseCmd(String cmd) {
  cmd.trim();
  Serial.println("CMD: " + cmd);

  if (cmd.startsWith("MODE:")) {
    int m = cmd.substring(5).toInt();
    if (m >= 0 && m <= 13) { currentMode = m; bleSend("OK:MODE:" + String(currentMode)); }

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
      int idx = cmd.substring(4, p1).toInt();
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
    currentMode = 6; strip.clear(); strip.show(); bleSend("OK:OFF");

  } else if (cmd == "RESET") {
    currentMode = 0; brightness = 180;
    pastelR[0]=4;  pastelG[0]=85;  pastelB[0]=42;
    pastelR[1]=35; pastelG[1]=80;  pastelB[1]=160;
    pastelR[2]=100;pastelG[2]=60;  pastelB[2]=120;
    pastelR[3]=180;pastelG[3]=40;  pastelB[3]=20;
    strip.setBrightness(brightness); bleSend("OK:RESET");

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

// ═══════════════════════════════════════════════════════════
//  PAGE WEB (interface iOS/Android)
// ═══════════════════════════════════════════════════════════
void handleRoot() {
  String html = R"rawhtml(
<!DOCTYPE html><html lang="fr"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>VIGILED</title>
<style>
  :root{--accent:#6c63ff;--bg:#0e0e1a;--card:#1a1a2e;--text:#e0e0ff}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);font-family:-apple-system,sans-serif;padding:16px;max-width:480px;margin:auto}
  h1{text-align:center;font-size:1.6rem;margin-bottom:20px;color:var(--accent)}
  .card{background:var(--card);border-radius:16px;padding:16px;margin-bottom:14px}
  h2{font-size:1rem;margin-bottom:12px;opacity:.7}
  .btn{display:inline-block;padding:10px 14px;border-radius:12px;border:none;
       background:var(--accent);color:#fff;font-size:.9rem;cursor:pointer;margin:4px;
       transition:opacity .2s}
  .btn:active{opacity:.6}
  .btn.off{background:#555}
  .btn.police{background:#c0392b}
  .btn.active{outline:3px solid #fff}
  .row{display:flex;flex-wrap:wrap;gap:6px}
  label{display:block;margin-bottom:6px;font-size:.85rem;opacity:.7}
  input[type=range]{width:100%;accent-color:var(--accent);margin-top:4px}
  .swatch{width:40px;height:40px;border-radius:10px;border:2px solid #555;cursor:pointer}
  .swatches{display:flex;gap:8px;flex-wrap:wrap}
  #status{margin-top:16px;text-align:center;font-size:.8rem;opacity:.5}
</style></head><body>
<h1>💡 VIGILED</h1>

<div class="card">
  <h2>MODE</h2>
  <div class="row">
    <button class="btn" onclick="send('MODE:0')">Pastel 0</button>
    <button class="btn" onclick="send('MODE:1')">Pastel 1</button>
    <button class="btn" onclick="send('MODE:2')">Pastel 2</button>
    <button class="btn" onclick="send('MODE:3')">Pastel 3</button>
    <button class="btn" onclick="send('MODE:4')">🌈 Arc-en-ciel</button>
    <button class="btn" onclick="send('MODE:5')">🇫🇷 Drapeau</button>
    <button class="btn" onclick="send('MODE:7')">🌊 Vague</button>
    <button class="btn" onclick="send('MODE:11')">🌙 Veille</button>
    <button class="btn" onclick="send('MODE:12')">⬜ Fixe</button>
    <button class="btn" onclick="send('MODE:13')">🚗 K2000</button>
    <button class="btn police" onclick="send('MODE:10')">🚨 Police</button>
    <button class="btn off" onclick="send('OFF')">OFF</button>
    <button class="btn off" onclick="send('RESET')">↺ Reset</button>
  </div>
</div>

<div class="card">
  <h2>LUMINOSITÉ</h2>
  <label>Valeur : <span id="briVal">180</span></label>
  <input type="range" min="80" max="255" value="180" id="briSlider"
         oninput="document.getElementById('briVal').textContent=this.value"
         onchange="send('BRI:'+this.value)">
</div>

<div class="card">
  <h2>COULEURS PASTEL</h2>
  <div class="swatches" id="swatches"></div>
  <div id="colorPickers" style="margin-top:12px"></div>
</div>

<div id="status">Connexion...</div>

<script>
const colors=[
  [4,85,42],[35,80,160],[100,60,120],[180,40,20]
];

function toHex(r,g,b){return '#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('')}
function fromHex(h){const v=parseInt(h.slice(1),16);return[(v>>16)&255,(v>>8)&255,v&255]}

function buildSwatches(){
  const sw=document.getElementById('swatches');
  const cp=document.getElementById('colorPickers');
  sw.innerHTML=''; cp.innerHTML='';
  colors.forEach((c,i)=>{
    const s=document.createElement('input');
    s.type='color'; s.value=toHex(...c);
    s.style.cssText='width:48px;height:48px;border:none;border-radius:10px;cursor:pointer;background:none;padding:0';
    s.onchange=e=>{
      const [r,g,b]=fromHex(e.target.value);
      colors[i]=[r,g,b];
      send('COL:'+i+':'+r+':'+g+':'+b);
    };
    sw.appendChild(s);
  });
}
buildSwatches();

async function send(cmd){
  document.getElementById('status').textContent='Envoi : '+cmd;
  try{
    const r=await fetch('/cmd?c='+encodeURIComponent(cmd));
    const t=await r.text();
    document.getElementById('status').textContent='✓ '+t;
  }catch(e){
    document.getElementById('status').textContent='⚠ Erreur réseau';
  }
}

// Récupère le statut au chargement
async function getStatus(){
  try{
    const r=await fetch('/cmd?c=STATUS');
    const t=await r.text();
    const m=t.match(/BRI=(\d+)/);
    if(m){
      const v=parseInt(m[1]);
      document.getElementById('briSlider').value=v;
      document.getElementById('briVal').textContent=v;
    }
    document.getElementById('status').textContent='Connecté ✓';
  }catch(e){
    document.getElementById('status').textContent='⚠ Impossible de joindre le contrôleur';
  }
}
getStatus();
</script>
</body></html>
)rawhtml";
  server.send(200, "text/html", html);
}

void handleCmd() {
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "ERR:NO_CMD");
    return;
  }
  String cmd = server.arg("c");
  parseCmd(cmd);
  // Réponse JSON simple
  String resp = "OK:" + cmd;
  if (cmd == "STATUS") {
    resp = "STATUS:MODE=" + String(currentMode) + ",BRI=" + String(brightness);
  }
  server.send(200, "text/plain", resp);
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
  if (btn && etatBtnMode == 1) {
    if (!longPressTraite && millis() - tempsAppuiMode > 2000) {
      longPressTraite = true; etatBtnMode = 2;
      currentMode = (currentMode == 10) ? 0 : 10;
      Serial.println("Long MODE → " + String(currentMode));
    }
  }
  if (!btn && etatBtnMode == 1) {
    if (millis() - tempsAppuiMode < 2000 && currentMode != 10) {
      const uint8_t cycle[] = { 0,1,2,3,4,5,7,11,12,13,6 };
      const uint8_t cycleLen = sizeof(cycle);
      uint8_t idx = 0;
      for (uint8_t i = 0; i < cycleLen; i++) { if (cycle[i] == currentMode) { idx = i; break; } }
      currentMode = cycle[(idx + 1) % cycleLen];
      Serial.println("Court MODE → " + String(currentMode));
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
    unsigned long maintenant = millis();
    if (maintenant - tempsAppuiBright > 400 && maintenant - dernierAjustBri > 80) {
      dernierAjustBri = maintenant;
      if (brightUp) {
        if (brightness < 255) { brightness += 5; if (brightness > 255) brightness = 255; strip.setBrightness(brightness); }
        if (brightness >= 255 && !flashActif) declencherFlash(true);
      } else {
        if (brightness > 25) { brightness -= 5; if (brightness < 25) brightness = 25; strip.setBrightness(brightness); }
        if (brightness <= 25 && !flashActif) declencherFlash(false);
      }
    }
  }
  if (!btn && etatBtnBright == 1) {
    if (millis() - tempsAppuiBright < 400) { brightUp = !brightUp; }
    etatBtnBright = 0;
  }
}

// ─── Flash limite ─────────────────────────────────────────
void gererFlashLimite() {
  if (!flashActif) return;
  if (millis() - flashDernier < 100) return;
  flashDernier = millis();
  if (flashEtape % 2 == 0) {
    strip.setBrightness(255);
    uint32_t col = flashEstMax ? strip.Color(255,255,255) : strip.Color(200,30,30);
    strip.fill(col, 0, LEDS_ACTIVE); strip.show();
  } else {
    strip.clear(); strip.show();
  }
  flashEtape++;
  if (flashEtape >= 6) {
    flashActif = false; flashEtape = 0; dernierAjustBri = millis();
    brightUp = !brightUp; strip.setBrightness(brightness);
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

  // ── WiFi Access Point ────────────────────────────────────
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.println("WiFi AP : " + String(AP_SSID));
  Serial.println("IP      : " + ip.toString());  // → 192.168.4.1

  server.on("/",    handleRoot);
  server.on("/cmd", handleCmd);
  server.begin();
  Serial.println("Serveur web démarré");

  // ── BLE ──────────────────────────────────────────────────
  BLEDevice::init("LED-Controller");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic* pRxChar = pService->createCharacteristic(
    CHAR_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxChar->setCallbacks(new RxCallbacks());

  pTxChar = pService->createCharacteristic(CHAR_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxChar->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE démarré : LED-Controller");
}

// ─── LOOP ─────────────────────────────────────────────────
void loop() {
  // Traitement des requêtes web
  server.handleClient();

  // Reconnexion BLE auto
  if (!bleConnected && oldConnected) {
    delay(300);
    pServer->startAdvertising();
    oldConnected = false;
  }
  if (bleConnected && !oldConnected) oldConnected = true;

  // Commande BLE entrante
  if (cmdPending) {
    cmdPending = false;
    parseCmd(pendingCmd);
  }

  // Boutons physiques
  gererBoutonMode();
  gererBoutonBright();

  // Flash limite luminosité
  gererFlashLimite();
  if (flashActif) return;

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
