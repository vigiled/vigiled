/*
 * ═══════════════════════════════════════════════════════════
 *  XIAO ESP32C6 — VIGILED  [ WiFi AP + UI embarquée ]
 *
 *  L'ESP32 crée son propre réseau ET sert la page de contrôle.
 *  Tout fonctionne en HTTP local → 100% compatible Safari iOS.
 *
 *  1. Flasher ce code
 *  2. iPhone : Réglages → WiFi → VIGILED (mdp: 12345678)
 *  3. Safari → http://192.168.4.1   (ou l'alerte "Se connecter" suffit)
 *  4. L'interface s'affiche, tout fonctionne offline
 *
 *  Bibliothèque requise : Adafruit NeoPixel
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

uint8_t       etatBtnMode     = 0;
unsigned long tempsAppuiMode  = 0;
bool          longPressTraite = false;

uint8_t       etatBtnBright    = 0;
unsigned long tempsAppuiBright = 0;
unsigned long dernierAjustBri  = 0;

bool          flashActif   = false;
uint8_t       flashEtape   = 0;
unsigned long flashDernier = 0;
bool          flashEstMax  = true;

// ─── WiFi on-demand ───────────────────────────────────────
#define WIFI_TIMEOUT_MS  300000UL  // 5 min sans requête → extinction
bool          wifiActif       = false;
unsigned long derniereRequete = 0;

// Double-clic bouton BRIGHT
uint8_t       clicCount   = 0;
unsigned long dernierClic = 0;
#define DOUBLE_CLIC_MS    400      // fenêtre entre 2 clics (ms)

// ─── Page HTML (stockée en flash programme) ───────────────
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>VIGILED</title>
<style>
:root{--bg:#0d0f14;--s:#13161d;--s2:#1a1e28;--b:#252a38;--a:#00e5a0;--a2:#00b8ff;--t:#e8eaf0;--td:#6b7280;--tm:#9ca3af;--r:#ff4757}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--t);font-family:-apple-system,sans-serif;padding:16px 14px 60px;max-width:460px;margin:auto}
h1{font-family:monospace;color:var(--a);font-size:1.3rem;margin:18px 0 22px;display:flex;align-items:center;gap:10px}
.pill{margin-left:auto;display:flex;align-items:center;gap:6px;background:var(--s2);border:1px solid var(--b);border-radius:20px;padding:4px 12px;font-size:11px;font-family:monospace;color:var(--td)}
.pill.on{border-color:rgba(0,229,160,.4);color:var(--a);background:rgba(0,229,160,.08)}
.dot{width:7px;height:7px;border-radius:50%;background:var(--td)}
.pill.on .dot{background:var(--a);box-shadow:0 0 6px var(--a);animation:p 2s infinite}
@keyframes p{0%,100%{opacity:1}50%{opacity:.4}}
.card{background:var(--s);border:1px solid var(--b);border-radius:12px;padding:16px;margin-bottom:10px}
.ct{font-family:monospace;font-size:10px;text-transform:uppercase;letter-spacing:.1em;color:var(--td);margin-bottom:14px}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:7px}
.mb{background:var(--s2);border:1px solid var(--b);border-radius:9px;padding:9px 3px;display:flex;flex-direction:column;align-items:center;gap:4px;cursor:pointer;-webkit-tap-highlight-color:transparent}
.mb:active{opacity:.6}.mb.act{border-color:var(--a);background:rgba(0,229,160,.1)}
.mi{font-size:19px}.ml{font-size:10px;color:var(--td);text-align:center;line-height:1.3}
.mb.act .ml{color:var(--a)}
.slrow{display:flex;justify-content:space-between;margin-bottom:8px;font-size:12px}
.slv{font-family:monospace;color:var(--a)}
input[type=range]{-webkit-appearance:none;width:100%;height:4px;border-radius:4px;background:var(--b);outline:none}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:var(--a);cursor:pointer;box-shadow:0 0 8px rgba(0,229,160,.4)}
.qb{display:flex;gap:6px;margin-top:12px}
.qb button{flex:1;padding:7px 2px;background:var(--s2);border:1px solid var(--b);border-radius:7px;color:var(--td);font-size:11px;cursor:pointer;font-family:monospace;-webkit-tap-highlight-color:transparent}
.qb button:active{opacity:.6}
.cg{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:12px}
.ci{display:flex;flex-direction:column;align-items:center;gap:5px}
.cp{width:42px;height:42px;border-radius:50%;border:2px solid var(--b);position:relative;overflow:hidden;cursor:pointer}
.cp input{position:absolute;opacity:0;width:100%;height:100%;cursor:pointer}
.cl{font-size:10px;color:var(--td);font-family:monospace}
.ag{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.ab{padding:12px;background:var(--s2);border:1px solid var(--b);border-radius:9px;color:var(--tm);font-size:13px;cursor:pointer;display:flex;align-items:center;gap:8px;-webkit-tap-highlight-color:transparent}
.ab:active{opacity:.6}.ab.off{grid-column:1/-1}
.ab.off:active{background:rgba(255,71,87,.1)}
.log{background:#080a0e;border:1px solid var(--b);border-radius:9px;padding:10px;height:110px;overflow-y:auto;font-family:monospace;font-size:11px;line-height:1.7}
.lo{color:var(--td)}.out{color:var(--a)}.in{color:var(--a2)}.err{color:var(--r)}.inf{color:#ffa040}
.toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%) translateY(80px);background:var(--s2);border:1px solid var(--b);border-radius:10px;padding:9px 20px;font-size:13px;z-index:99;transition:transform .3s;pointer-events:none;white-space:nowrap}
.toast.show{transform:translateX(-50%) translateY(0)}
.toast.ok{border-color:rgba(0,229,160,.4);color:var(--a)}
.toast.er{border-color:rgba(255,71,87,.4);color:var(--r)}
</style>
</head>
<body>
<h1>💡 VIGI·LED
  <span class="pill" id="pill"><span class="dot"></span><span id="pt">Connexion...</span></span>
</h1>

<div class="card">
  <div class="ct">Mode d'affichage</div>
  <div class="grid">
    <div class="mb act" data-m="0"  onclick="setMode(0)"> <span class="mi">🌿</span><span class="ml">Vert</span></div>
    <div class="mb"     data-m="1"  onclick="setMode(1)"> <span class="mi">💙</span><span class="ml">Bleu</span></div>
    <div class="mb"     data-m="2"  onclick="setMode(2)"> <span class="mi">💜</span><span class="ml">Violet</span></div>
    <div class="mb"     data-m="3"  onclick="setMode(3)"> <span class="mi">🧡</span><span class="ml">Orange</span></div>
    <div class="mb"     data-m="4"  onclick="setMode(4)"> <span class="mi">🌈</span><span class="ml">Arc-en-ciel</span></div>
    <div class="mb"     data-m="5"  onclick="setMode(5)"> <span class="mi">🇫🇷</span><span class="ml">Drapeau</span></div>
    <div class="mb"     data-m="7"  onclick="setMode(7)"> <span class="mi">🌊</span><span class="ml">Vague</span></div>
    <div class="mb"     data-m="10" onclick="setMode(10)"><span class="mi">🚨</span><span class="ml">Police</span></div>
    <div class="mb"     data-m="11" onclick="setMode(11)"><span class="mi">😴</span><span class="ml">Veille</span></div>
    <div class="mb"     data-m="12" onclick="setMode(12)"><span class="mi">💡</span><span class="ml">Fixe</span></div>
    <div class="mb"     data-m="13" onclick="setMode(13)"><span class="mi">🔴</span><span class="ml">K-2000</span></div>
    <div class="mb"     data-m="6"  onclick="setMode(6)"> <span class="mi">⬛</span><span class="ml">Éteint</span></div>
  </div>
</div>

<div class="card">
  <div class="ct">Luminosité</div>
  <div class="slrow"><span>Intensité</span><span class="slv" id="bv">70%</span></div>
  <input type="range" id="bs" min="80" max="255" value="180"
         oninput="updBri(this.value)" onchange="send('BRI:'+this.value)">
  <div class="qb">
    <button onclick="setBri(80)">25%</button>
    <button onclick="setBri(127)">50%</button>
    <button onclick="setBri(191)">75%</button>
    <button onclick="setBri(255)">MAX</button>
  </div>
</div>

<div class="card">
  <div class="ct">Couleurs de respiration</div>
  <div class="cg" id="cg"></div>
  <div class="ab" style="justify-content:center;font-size:13px" onclick="sendColors()">↑ Envoyer les couleurs</div>
</div>

<div class="card">
  <div class="ct">Actions</div>
  <div class="ag">
    <div class="ab" onclick="send('STATUS')">📊 Statut</div>
    <div class="ab" onclick="send('RESET')">🔄 Reset</div>
    <div class="ab off" onclick="send('OFF')">⏻ Tout éteindre</div>
  </div>
</div>

<div class="card">
  <div class="ct">Console</div>
  <div class="log" id="log"></div>
</div>

<div class="toast" id="toast"></div>

<script>
const colors=[{r:4,g:85,b:42},{r:35,g:80,b:160},{r:100,g:60,b:120},{r:180,g:40,b:20}];
let ok=false;

function h2(n){return n.toString(16).padStart(2,'0')}
function toHex(r,g,b){return '#'+h2(r)+h2(g)+h2(b)}
function fromHex(h){const v=parseInt(h.slice(1),16);return[(v>>16)&255,(v>>8)&255,v&255]}

function buildColors(){
  const g=document.getElementById('cg');
  g.innerHTML='';
  colors.forEach((c,i)=>{
    const inp=document.createElement('input');
    inp.type='color'; inp.value=toHex(c.r,c.g,c.b);
    inp.onchange=e=>{const[r,g,b]=fromHex(e.target.value);colors[i]={r,g,b};e.target.parentElement.style.background=e.target.value};
    const cp=document.createElement('div'); cp.className='cp';
    cp.style.background=inp.value; cp.appendChild(inp);
    const lbl=document.createElement('div'); lbl.className='cl'; lbl.textContent='C'+(i+1);
    const ci=document.createElement('div'); ci.className='ci';
    ci.appendChild(cp); ci.appendChild(lbl);
    g.appendChild(ci);
  });
}

function updBri(v){
  document.getElementById('bv').textContent=Math.round((v-80)/175*100)+'%';
}
function setBri(v){
  document.getElementById('bs').value=v; updBri(v); send('BRI:'+v);
}

// Met à jour uniquement l'affichage des boutons (sans envoyer de commande)
function highlightMode(n){
  document.querySelectorAll('.mb').forEach(b=>{
    const m=b.getAttribute('data-m');
    b.classList.toggle('act', m!==null && parseInt(m)===n);
  });
}

// Appelé par l'utilisateur : envoie la commande ET met à jour l'UI
function setMode(n){
  highlightMode(n);
  send('MODE:'+n);
}

function sendColors(){
  colors.forEach((c,i)=>setTimeout(()=>send('COL:'+i+':'+c.r+':'+c.g+':'+c.b),i*100));
  toast('Couleurs envoyées','ok');
}

async function send(cmd){
  log('out','→ '+cmd);
  try{
    const r=await fetch('/cmd?c='+encodeURIComponent(cmd));
    const t=await r.text();
    log('in','← '+t);
    handleResp(t);
  }catch(e){log('err','Erreur: '+e.message)}
}

function handleResp(msg){
  if(msg.startsWith('STATUS:')){
    const pairs=msg.replace('STATUS:','').split(',');
    pairs.forEach(p=>{
      const[k,v]=p.split('=');
      if(k==='MODE') highlightMode(parseInt(v));
      if(k==='BRI'){document.getElementById('bs').value=v;updBri(v);}
    });
  } else if(msg.startsWith('OK:MODE:')){
    highlightMode(parseInt(msg.split(':')[2]));
  }
}

function log(type,msg){
  const c=document.getElementById('log');
  const d=document.createElement('div'); d.className=type;
  const ts=new Date().toLocaleTimeString('fr-FR',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
  d.textContent='['+ts+'] '+msg; c.appendChild(d); c.scrollTop=c.scrollHeight;
}

let _tt;
function toast(msg,type){
  const t=document.getElementById('toast');
  t.textContent=msg; t.className='toast '+type+' show';
  clearTimeout(_tt); _tt=setTimeout(()=>t.classList.remove('show'),2500);
}

// Connexion auto au chargement
(async()=>{
  log('inf','Page chargée depuis l\'ESP32 ✓');
  try{
    const r=await fetch('/cmd?c=STATUS');
    const t=await r.text();
    document.getElementById('pill').classList.add('on');
    document.getElementById('pt').textContent='Connecté';
    handleResp(t);
    log('inf','ESP32 connecté ✓');
    toast('Connecté ✓','ok');
  }catch(e){
    log('err','Erreur connexion: '+e.message);
    document.getElementById('pt').textContent='Erreur';
  }
})();

buildColors();
</script>
</body>
</html>
)=====";

// ─── Parser commandes ─────────────────────────────────────
String parseCmd(String cmd) {
  cmd.trim();
  Serial.println("CMD: " + cmd);

  if (cmd.startsWith("MODE:")) {
    int m = cmd.substring(5).toInt();
    if (m >= 0 && m <= 13) { currentMode = m; }
    return "OK:MODE:" + String(currentMode);

  } else if (cmd.startsWith("BRI:")) {
    int b = cmd.substring(4).toInt();
    if (b >= 80 && b <= 255) { brightness = b; strip.setBrightness(brightness); }
    return "OK:BRI:" + String(brightness);

  } else if (cmd.startsWith("COL:")) {
    int p1 = cmd.indexOf(':', 4);
    int p2 = cmd.indexOf(':', p1+1);
    int p3 = cmd.indexOf(':', p2+1);
    if (p3 > 0) {
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
void handleRoot() {
  derniereRequete = millis();
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCmd() {
  derniereRequete = millis();  // reset timeout à chaque commande
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("c")) { server.send(400, "text/plain", "ERR:NO_CMD"); return; }
  server.send(200, "text/plain", parseCmd(server.arg("c")));
}

void handleNotFound() {
  derniereRequete = millis();
  server.sendHeader("Location", "http://192.168.4.1", true);
  server.send(302, "text/plain", "");
}

// ─── WiFi on / off ────────────────────────────────────────
void wifiStart() {
  if (wifiActif) return;
  Serial.println("WiFi ON");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  server.begin();
  wifiActif = true;
  derniereRequete = millis();
  // Feedback : 3 flashs bleus
  for (int i = 0; i < 3; i++) {
    strip.setBrightness(180);
    strip.fill(strip.Color(0, 80, 255), 0, LEDS_ACTIVE); strip.show(); delay(120);
    strip.clear(); strip.show(); delay(80);
  }
  strip.setBrightness(brightness);
  Serial.println("AP pret : VIGILED — 192.168.4.1");
}

void wifiStop() {
  if (!wifiActif) return;
  Serial.println("WiFi OFF (timeout)");
  server.stop();
  WiFi.mode(WIFI_OFF);
  wifiActif = false;
  // Feedback : 2 flashs oranges
  for (int i = 0; i < 2; i++) {
    strip.setBrightness(180);
    strip.fill(strip.Color(200, 80, 0), 0, LEDS_ACTIVE); strip.show(); delay(120);
    strip.clear(); strip.show(); delay(80);
  }
  strip.setBrightness(brightness);
}

void gererWifi() {
  if (!wifiActif) return;
  server.handleClient();
  if (millis() - derniereRequete > WIFI_TIMEOUT_MS) wifiStop();
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
  strip.fill(strip.Color(
    (uint32_t)pastelR[idx] * bri / 255 * brightness / 255,
    (uint32_t)pastelG[idx] * bri / 255 * brightness / 255,
    (uint32_t)pastelB[idx] * bri / 255 * brightness / 255
  ), 0, LEDS_ACTIVE);
  strip.show();
}

void rainbow() {
  static uint16_t j = 0;
  for (uint8_t i = 0; i < LEDS_ACTIVE; i++)
    strip.setPixelColor(i, strip.ColorHSV((j + i * 8192UL) % 65536, 255, brightness));
  strip.show(); j = (j + 256) % 65536; delay(20);
}

void drapeauFrancais() {
  static float phase = 0.0f;
  phase += 0.07f; if (phase > TWO_PI) phase -= TWO_PI;
  for (int i = 0; i < LEDS_ACTIVE; i++) {
    float pos  = (float)i / (LEDS_ACTIVE-1);
    float wave = sinf(pos * PI * 3.5f + phase) * 0.35f + 0.65f;
    uint8_t w  = (uint8_t)(255 * wave);
    strip.setPixelColor(i,
      pos < 0.333f ? strip.Color(0,0,w) :
      pos < 0.666f ? strip.Color(w,w,w) : strip.Color(w,0,0));
  }
  strip.show(); delay(30);
}

void vagueFluide() {
  static unsigned long t0 = millis();
  float t = (millis()-t0)/1000.0f;
  for (int i = 0; i < LEDS_ACTIVE; i++) {
    float bri = sinf(((float)i/(LEDS_ACTIVE-1))*TWO_PI - t*0.8f)*0.4f+0.6f;
    strip.setPixelColor(i, strip.Color(
      (uint8_t)(20*bri*brightness/255),
      (uint8_t)(80*bri*brightness/255),
      (uint8_t)(160*bri*brightness/255)));
  }
  strip.show(); delay(20);
}

void policeMode() {
  static uint8_t step=0; static unsigned long last=0;
  if (millis()-last < 50) return; last=millis();
  strip.clear();
  if (step<8)       { if(step%2==0)     strip.fill(0xFF0000); }
  else if (step<16) { if((step-8)%2==0) strip.fill(0x0000FF); }
  strip.show();
  if (++step>=18) { step=0; delay(380); }
}

void veilleDiscrete() {
  static unsigned long t0=millis();
  float t=(float)((millis()-t0)%4000)/4000.0f;
  float sc=((sinf(t*TWO_PI-HALF_PI)+1.0f)/2.0f)*0.35f+0.02f;
  strip.clear();
  int mid=LEDS_ACTIVE/2;
  uint8_t r=(uint8_t)(40*sc*brightness/255),g=(uint8_t)(130*sc*brightness/255),b=(uint8_t)(80*sc*brightness/255);
  strip.setPixelColor(mid,strip.Color(r,g,b));
  if (mid>0)             strip.setPixelColor(mid-1,strip.Color(r/4,g/4,b/4));
  if (mid<LEDS_ACTIVE-1) strip.setPixelColor(mid+1,strip.Color(r/4,g/4,b/4));
  strip.show(); delay(20);
}

void fixeStable() {
  strip.fill(strip.Color(
    (uint32_t)pastelR[0]*brightness/255,
    (uint32_t)pastelG[0]*brightness/255,
    (uint32_t)pastelB[0]*brightness/255
  ), 0, LEDS_ACTIVE);
  strip.show(); delay(100);
}

void k2000Rouge() {
  static int pos=0,dir=1; static unsigned long last=0;
  if (millis()-last < 55) return; last=millis();
  strip.clear();
  for (int i=0;i<LEDS_ACTIVE;i++) {
    int d=abs(i-pos);
    uint8_t rv=(d==0)?255:(d==1)?100:(d==2)?30:0;
    strip.setPixelColor(i,strip.Color(rv*brightness/255,0,0));
  }
  strip.show();
  pos+=dir;
  if (pos>=LEDS_ACTIVE-1){pos=LEDS_ACTIVE-1;dir=-1;}
  if (pos<=0)            {pos=0;            dir= 1;}
}

// ─── Bouton MODE ──────────────────────────────────────────
void gererBoutonMode() {
  bool btn = (digitalRead(BTN_MODE_PIN)==LOW);
  if (btn && etatBtnMode==0) { etatBtnMode=1; tempsAppuiMode=millis(); longPressTraite=false; }
  if (btn && etatBtnMode==1 && !longPressTraite && millis()-tempsAppuiMode>2000) {
    longPressTraite=true; etatBtnMode=2;
    currentMode=(currentMode==10)?0:10;
  }
  if (!btn && etatBtnMode==1) {
    if (millis()-tempsAppuiMode<2000 && currentMode!=10) {
      const uint8_t cy[]={0,1,2,3,4,5,7,11,12,13,6};
      uint8_t idx=0;
      for (uint8_t i=0;i<sizeof(cy);i++) if(cy[i]==currentMode){idx=i;break;}
      currentMode=cy[(idx+1)%sizeof(cy)];
    }
    etatBtnMode=0;
  }
  if (!btn && etatBtnMode==2) etatBtnMode=0;
}

// ─── Bouton BRIGHT (+ double-clic = WiFi on/off) ─────────
void declencherFlash(bool estMax) {
  flashActif=true; flashEtape=0; flashDernier=millis(); flashEstMax=estMax;
}

void gererBoutonBright() {
  bool btn = (digitalRead(BTN_BRIGHT_PIN)==LOW);

  // ── Détection double-clic (appuis courts successifs) ──
  if (btn && etatBtnBright==0) {
    etatBtnBright=1; tempsAppuiBright=millis(); dernierAjustBri=millis();
  }
  if (!btn && etatBtnBright==1) {
    unsigned long duree = millis() - tempsAppuiBright;
    if (duree < 400) {
      // C'est un appui court — compter les clics
      unsigned long maintenant = millis();
      if (maintenant - dernierClic < DOUBLE_CLIC_MS) {
        // Double-clic détecté !
        clicCount = 0;
        dernierClic = 0;
        if (wifiActif) wifiStop(); else wifiStart();
        etatBtnBright = 0;
        return;
      } else {
        clicCount = 1;
        dernierClic = maintenant;
        // Le 1er clic inverse la direction (comportement original)
        brightUp = !brightUp;
      }
    }
    etatBtnBright = 0;
  }

  // ── Appui long : ajustement continu luminosité ────────
  if (btn && etatBtnBright==1 && !flashActif) {
    unsigned long now = millis();
    if (now-tempsAppuiBright>400 && now-dernierAjustBri>80) {
      dernierAjustBri = now;
      clicCount = 0; // annule le double-clic en cours
      if (brightUp) {
        brightness=min(255, brightness+5); strip.setBrightness(brightness);
        if (brightness>=255 && !flashActif) declencherFlash(true);
      } else {
        brightness=max(25, brightness-5); strip.setBrightness(brightness);
        if (brightness<=25  && !flashActif) declencherFlash(false);
      }
    }
  }
}

// ─── Flash limite ─────────────────────────────────────────
void gererFlashLimite() {
  if (!flashActif || millis()-flashDernier<100) return;
  flashDernier=millis();
  if (flashEtape%2==0) {
    strip.setBrightness(255);
    strip.fill(flashEstMax?strip.Color(255,255,255):strip.Color(200,30,30),0,LEDS_ACTIVE);
    strip.show();
  } else { strip.clear(); strip.show(); }
  if (++flashEtape>=6) {
    flashActif=false; flashEtape=0; dernierAjustBri=millis();
    brightUp=!brightUp; strip.setBrightness(brightness);
  }
}

// ─── SETUP ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BTN_MODE_PIN,   INPUT_PULLUP);
  pinMode(BTN_BRIGHT_PIN, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(brightness);
  strip.clear(); strip.show();

  // Enregistrer les routes (le serveur sera démarré par wifiStart)
  server.on("/",        handleRoot);
  server.on("/cmd",     handleCmd);
  server.onNotFound(handleNotFound);

  Serial.println("VIGILED pret — double-clic BRIGHT pour activer le WiFi");
}

// ─── LOOP ─────────────────────────────────────────────────
void loop() {
  gererWifi();  // gère les requêtes HTTP + timeout 5 min

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
