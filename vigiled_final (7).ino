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
#include <Preferences.h>

// ─── Broches ──────────────────────────────────────────────
#define PIN_NEOPIXEL   1
#define NUM_LEDS       28
#define LEDS_ACTIVE    9
#define BTN_MODE_PIN   2
#define BTN_BRIGHT_PIN 21

// ─── WiFi AP ──────────────────────────────────────────────
const char* AP_SSID = "VIGILED";
const char* AP_PASS = "12345678";
WebServer   server(80);
Preferences prefs;

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
#define WIFI_TIMEOUT_MS  300000UL
bool          wifiActif       = false;
unsigned long derniereRequete = 0;

uint8_t       clicCount   = 0;
unsigned long dernierClic = 0;
#define DOUBLE_CLIC_MS    400

// ─── Mode nuit ────────────────────────────────────────────
// nuitMode : 0=désactivé  1=manuel  2=auto (timer)
uint8_t  nuitMode     = 0;
bool     nuitManOn    = false;   // état ON/OFF en mode manuel
uint8_t  nuitHeureOn  = 20;      // heure allumage (mode auto)
uint8_t  nuitMinOn    = 0;
uint8_t  nuitHeureOff = 7;       // heure extinction (mode auto)
uint8_t  nuitMinOff   = 0;

// Heure courante synchronisée depuis l'iPhone
bool     heureConnue  = false;
uint8_t  heureActuelle = 0;
uint8_t  minActuelle   = 0;
unsigned long derniereSyncMs = 0;  // millis() au moment de la sync

bool ledsAutorisees() {
  // Retourne true si les LEDs doivent être allumées selon le mode nuit
  if (nuitMode == 0) return true;                   // désactivé → toujours ON
  if (nuitMode == 1) return nuitManOn;              // manuel → selon bouton
  // mode auto
  if (!heureConnue) return true;                    // heure inconnue → ON par défaut
  // Calcul heure courante (heure sync + temps écoulé)
  unsigned long elapsed = (millis() - derniereSyncMs) / 1000UL;
  uint16_t totalMin = heureActuelle * 60 + minActuelle + elapsed / 60;
  totalMin %= 1440;  // modulo 24h
  uint16_t minOn  = nuitHeureOn  * 60 + nuitMinOn;
  uint16_t minOff = nuitHeureOff * 60 + nuitMinOff;
  if (minOn < minOff) return (totalMin >= minOn && totalMin < minOff);
  else                return (totalMin >= minOn || totalMin < minOff); // chevauchement minuit
}

void sauvegarderPrefs() {
  prefs.begin("vigiled", false);
  prefs.putUChar("nuitMode",    nuitMode);
  prefs.putUChar("nHOn",        nuitHeureOn);
  prefs.putUChar("nMOn",        nuitMinOn);
  prefs.putUChar("nHOff",       nuitHeureOff);
  prefs.putUChar("nMOff",       nuitMinOff);
  prefs.putBool ("nManOn",      nuitManOn);
  prefs.end();
}

void chargerPrefs() {
  prefs.begin("vigiled", true);
  nuitMode     = prefs.getUChar("nuitMode", 0);
  nuitHeureOn  = prefs.getUChar("nHOn",     20);
  nuitMinOn    = prefs.getUChar("nMOn",      0);
  nuitHeureOff = prefs.getUChar("nHOff",     7);
  nuitMinOff   = prefs.getUChar("nMOff",     0);
  nuitManOn    = prefs.getBool ("nManOn",  false);
  prefs.end();
}


// ─── Manifest PWA (Android) ───────────────────────────────
const char MANIFEST_JSON[] PROGMEM = R"=====(
{
  "name": "VIGILED",
  "short_name": "VIGILED",
  "description": "Dashcam Protection Signal",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#0d0f14",
  "theme_color": "#0d0f14",
  "orientation": "portrait",
  "icons": [
    { "src": "/icon192.png", "sizes": "192x192", "type": "image/png", "purpose": "any maskable" },
    { "src": "/icon512.png", "sizes": "512x512", "type": "image/png", "purpose": "any maskable" }
  ]
}
)=====";

const char ICON_192[] PROGMEM = "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";
const char ICON_512[] PROGMEM = "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";

// ─── Page HTML (stockée en flash programme) ───────────────
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="VIGILED">
<meta name="theme-color" content="#0d0f14">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon192.png">
<title>VIGILED</title>
<style>
:root{--bg:#0d0f14;--s:#13161d;--s2:#1a1e28;--b:#252a38;--a:#00e5a0;--a2:#00b8ff;--t:#e8eaf0;--td:#6b7280;--tm:#9ca3af;--r:#ff4757}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--t);font-family:-apple-system,sans-serif;padding:16px 14px 60px;max-width:460px;margin:auto}
@supports(padding-top:env(safe-area-inset-top)){
  body{padding-top:calc(env(safe-area-inset-top) + 16px)}
}
#splash{position:fixed;inset:0;background:#0a0a0a;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:18px;z-index:999;cursor:pointer;transition:opacity .6s ease}
#splash.hide{opacity:0;pointer-events:none}
.sp-title{font:900 18vw/1 'Arial Black',sans-serif;letter-spacing:-2px;text-transform:uppercase;white-space:nowrap}
.sp-vigi{background:linear-gradient(180deg,#d0d8e8 0%,#a0b0c8 30%,#6a7a90 60%,#3a4a5a 100%);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.sp-led{background:linear-gradient(160deg,#ff0000,#ff6600,#ffee00,#00dd00,#00bbff,#3344ff,#aa00ff,#cc00cc,#ff0000,#ff6600,#ffee00,#00dd00,#00bbff,#3344ff,#aa00ff,#cc00cc);background-size:400% 100%;-webkit-background-clip:text;-webkit-text-fill-color:transparent;filter:drop-shadow(0 0 18px rgba(150,0,255,.5)) drop-shadow(0 0 8px rgba(0,200,255,.3));animation:wave 8s linear infinite}
@keyframes wave{0%{background-position:0% 50%}100%{background-position:100% 50%}}
.sp-tag{font:400 13px/1 'Trebuchet MS',sans-serif;letter-spacing:6px;color:#6a7a90;text-transform:uppercase}
.sp-hint{font-size:11px;color:#3a4a5a;letter-spacing:2px;animation:blink 1.5s infinite}
@keyframes blink{0%,100%{opacity:.3}50%{opacity:1}}
/* ── Header mini logo ── */
.hdr{display:flex;align-items:center;gap:10px;padding:18px 0 20px;border-bottom:1px solid var(--b);margin-bottom:14px}
.logo-mini{font:900 28px/1 'Arial Black',sans-serif;letter-spacing:-1px;text-transform:uppercase;flex-shrink:0}
.lm-vigi{background:linear-gradient(180deg,#d0d8e8 0%,#a0b0c8 40%,#6a7a90 100%);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.lm-led{background:linear-gradient(160deg,#ff0000,#ff6600,#ffee00,#00dd00,#00bbff,#3344ff,#aa00ff,#cc00cc,#ff0000,#ff6600,#ffee00,#00dd00,#00bbff,#3344ff,#aa00ff,#cc00cc);background-size:400% 100%;-webkit-background-clip:text;-webkit-text-fill-color:transparent;filter:drop-shadow(0 0 6px rgba(150,0,255,.4));animation:wave 8s linear infinite}
.logo-tag{font-size:9px;letter-spacing:3px;color:#3a4a5a;text-transform:uppercase;flex:1;line-height:1.4}
.hdr{display:flex;align-items:center;justify-content:space-between;margin:16px 0 20px}
.logo-mini{font:900 9vw/1 'Arial Black',sans-serif;letter-spacing:-1px;text-transform:uppercase;white-space:nowrap}
.lm-vigi{background:linear-gradient(180deg,#d0d8e8 0%,#a0b0c8 30%,#6a7a90 60%,#3a4a5a 100%);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.lm-led{background:linear-gradient(160deg,#ff0000,#ff6600,#ffee00,#00dd00,#00bbff,#3344ff,#aa00ff,#cc00cc,#ff0000,#ff6600,#ffee00,#00dd00,#00bbff,#3344ff,#aa00ff,#cc00cc);background-size:400% 100%;-webkit-background-clip:text;-webkit-text-fill-color:transparent;filter:drop-shadow(0 0 6px rgba(150,0,255,.4));animation:wave 8s linear infinite}
.logo-sub{font-size:8px;letter-spacing:3px;color:#3a4a5a;text-transform:uppercase;margin-top:3px}
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
/* ── Mode nuit ── */
.ntabs{display:flex;background:var(--s2);border-radius:8px;padding:3px;gap:3px;margin-bottom:16px}
.ntab{flex:1;padding:8px;border:none;border-radius:6px;background:transparent;color:var(--td);font-size:12px;cursor:pointer;transition:all .2s;font-family:-apple-system,sans-serif}
.ntab.act{background:var(--s);color:var(--t);box-shadow:0 1px 4px rgba(0,0,0,.4)}
.nrow{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
.nlbl{font-size:13px}
.nsub{font-size:11px;color:var(--td);margin-top:2px}
.tgl{width:44px;height:24px;background:var(--b);border-radius:12px;border:none;cursor:pointer;position:relative;transition:background .3s;flex-shrink:0}
.tgl::after{content:'';position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;background:#fff;transition:transform .3s}
.tgl.on{background:var(--a)}
.tgl.on::after{transform:translateX(20px)}
.tgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:4px}
.tfield{display:flex;flex-direction:column;gap:5px}
.tflbl{font-size:10px;color:var(--td);font-family:monospace;text-transform:uppercase;letter-spacing:.08em}
input[type=time]{background:var(--s2);border:1px solid var(--b);border-radius:8px;padding:9px 10px;color:var(--t);font-size:14px;width:100%;outline:none;-webkit-appearance:none}
input[type=time]:focus{border-color:rgba(0,184,255,.5)}
.nstatus{font-size:11px;color:var(--td);margin-top:10px;padding:8px 10px;background:var(--s2);border-radius:7px;text-align:center}
.nstatus.active{color:var(--a);background:rgba(0,229,160,.08)}
</style>
</head>
<body>
<div id="splash" onclick="dismissSplash()">
  <div class="sp-title">
    <span class="sp-vigi">VIGI</span><span class="sp-led">LED</span>
  </div>
  <div class="sp-tag">Dashcam Protection Signal</div>
  <div class="sp-hint">Appuyer pour continuer</div>
</div>

<div class="hdr">
  <div class="logo-mini">
    <span class="lm-vigi">VIGI</span><span class="lm-led">LED</span>
  </div>
  <div class="logo-tag">Dashcam Protection Signal</div>
  <span class="pill" id="pill"><span class="dot"></span><span id="pt">Connexion...</span></span>
</div>

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
  <div class="ct">🌙 Mode nuit</div>
  <div class="ntabs">
    <button class="ntab act" id="ntab0" onclick="setNuitMode(0)">Désactivé</button>
    <button class="ntab"     id="ntab1" onclick="setNuitMode(1)">Manuel</button>
    <button class="ntab"     id="ntab2" onclick="setNuitMode(2)">Auto ⏱</button>
  </div>

  <div id="npanel1" style="display:none">
    <div class="nrow">
      <div class="ntlbl">
        <div class="nlbl">LEDs activées</div>
        <div class="nsub">Allumer/éteindre manuellement</div>
      </div>
      <button class="tgl" id="manTgl" onclick="toggleManuel()"></button>
    </div>
  </div>

  <div id="npanel2" style="display:none">
    <div class="tgrid">
      <div class="tfield">
        <div class="tflbl">🌅 Allumage</div>
        <input type="time" id="tOn" value="20:00" onchange="sendNuitAuto()">
      </div>
      <div class="tfield">
        <div class="tflbl">🌄 Extinction</div>
        <input type="time" id="tOff" value="07:00" onchange="sendNuitAuto()">
      </div>
    </div>
  </div>

  <div class="nstatus" id="nstatus">Mode nuit désactivé</div>
</div>

<div class="card">
  <div class="ct">Console</div>
  <div class="log" id="log"></div>
</div>

<div class="toast" id="toast"></div>

<script>
const colors=[{r:4,g:85,b:42},{r:35,g:80,b:160},{r:100,g:60,b:120},{r:180,g:40,b:20}];
let ok=false;

// ── Splash ──
function dismissSplash(){
  const s=document.getElementById('splash');
  s.classList.add('hide');
  setTimeout(()=>s.remove(),650);
}
setTimeout(dismissSplash, 2500); // auto-dismiss après 2.5s

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
      if(k==='NMODE') applyNuitMode(parseInt(v),false);
      if(k==='NMON')  { document.getElementById('manTgl').classList.toggle('on',v==='1'); updNStatus(); }
      if(k==='NHON')  document.getElementById('tOn').value=v;
      if(k==='NHOFF') document.getElementById('tOff').value=v;
    });
  } else if(msg.startsWith('OK:MODE:')){
    highlightMode(parseInt(msg.split(':')[2]));
  } else if(msg.startsWith('OK:NUIT:')){
    updNStatus();
  }
}

// ── Mode nuit UI ──
function setNuitMode(m){
  applyNuitMode(m,true);
}
function applyNuitMode(m,sendIt){
  [0,1,2].forEach(i=>document.getElementById('ntab'+i).classList.toggle('act',i===m));
  document.getElementById('npanel1').style.display=m===1?'block':'none';
  document.getElementById('npanel2').style.display=m===2?'block':'none';
  updNStatus();
  if(sendIt) send('NUIT:MODE:'+m);
}
function toggleManuel(){
  const tgl=document.getElementById('manTgl');
  const on=!tgl.classList.contains('on');
  tgl.classList.toggle('on',on);
  updNStatus();
  send('NUIT:MAN:'+(on?1:0));
}
function sendNuitAuto(){
  const on=document.getElementById('tOn').value||'20:00';
  const off=document.getElementById('tOff').value||'07:00';
  send('NUIT:AUTO:'+on+':'+off);
  updNStatus();
}
function updNStatus(){
  const s=document.getElementById('nstatus');
  const tabs=document.querySelectorAll('.ntab');
  let m=0; tabs.forEach((t,i)=>{if(t.classList.contains('act'))m=i;});
  if(m===0){s.textContent='Mode nuit désactivé';s.className='nstatus';return;}
  if(m===1){
    const on=document.getElementById('manTgl').classList.contains('on');
    s.textContent=on?'LEDs activées manuellement ●':'LEDs éteintes manuellement ○';
    s.className='nstatus'+(on?' active':'');
    return;
  }
  const on=document.getElementById('tOn').value||'20:00';
  const off=document.getElementById('tOff').value||'07:00';
  s.textContent='Auto — allumage '+on+', extinction '+off;
  s.className='nstatus active';
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

// Connexion auto + sync heure au chargement
(async()=>{
  log('inf','Page chargée depuis l\'ESP32 ✓');
  // Sync heure iPhone → ESP32
  const now=new Date();
  const hh=now.getHours(), mm=now.getMinutes();
  try{ await fetch('/cmd?c='+encodeURIComponent('TIME:'+hh+':'+mm)); log('inf','Heure sync: '+String(hh).padStart(2,'0')+':'+String(mm).padStart(2,'0')); }
  catch(e){}
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

  // ── Sync heure depuis iPhone ──────────────────────────
  } else if (cmd.startsWith("TIME:")) {
    int sep = cmd.indexOf(':', 5);
    if (sep > 0) {
      heureActuelle  = cmd.substring(5, sep).toInt();
      minActuelle    = cmd.substring(sep+1).toInt();
      derniereSyncMs = millis();
      heureConnue    = true;
      Serial.printf("Heure sync: %02d:%02d\n", heureActuelle, minActuelle);
    }
    return "OK:TIME";

  // ── Mode nuit ─────────────────────────────────────────
  } else if (cmd.startsWith("NUIT:MODE:")) {
    nuitMode = constrain(cmd.substring(10).toInt(), 0, 2);
    sauvegarderPrefs();
    return "OK:NUIT:MODE:" + String(nuitMode);

  } else if (cmd.startsWith("NUIT:MAN:")) {
    nuitManOn = cmd.substring(9).toInt() == 1;
    sauvegarderPrefs();
    return "OK:NUIT:MAN:" + String(nuitManOn ? 1 : 0);

  } else if (cmd.startsWith("NUIT:AUTO:")) {
    // Format: NUIT:AUTO:HH:MM:HH:MM
    String s = cmd.substring(10);
    int c1=s.indexOf(':'), c2=s.indexOf(':',c1+1), c3=s.indexOf(':',c2+1);
    if (c3 > 0) {
      nuitHeureOn  = s.substring(0,    c1).toInt();
      nuitMinOn    = s.substring(c1+1, c2).toInt();
      nuitHeureOff = s.substring(c2+1, c3).toInt();
      nuitMinOff   = s.substring(c3+1).toInt();
      sauvegarderPrefs();
      Serial.printf("Nuit auto: ON=%02d:%02d OFF=%02d:%02d\n",
        nuitHeureOn,nuitMinOn,nuitHeureOff,nuitMinOff);
    }
    return "OK:NUIT:AUTO";

  } else if (cmd == "STATUS") {
    // Calcul heure courante pour affichage
    String heureCourante = "??:??";
    if (heureConnue) {
      unsigned long elapsed = (millis() - derniereSyncMs) / 1000UL;
      uint16_t tot = (heureActuelle*60 + minActuelle + elapsed/60) % 1440;
      heureCourante = String(tot/60).length()<2?"0"+String(tot/60):String(tot/60);
      heureCourante += ":";
      heureCourante += String(tot%60).length()<2?"0"+String(tot%60):String(tot%60);
    }
    return "STATUS:MODE=" + String(currentMode)
         + ",BRI=" + String(brightness)
         + ",R0=" + String(pastelR[0]) + ",G0=" + String(pastelG[0]) + ",B0=" + String(pastelB[0])
         + ",R1=" + String(pastelR[1]) + ",G1=" + String(pastelG[1]) + ",B1=" + String(pastelB[1])
         + ",R2=" + String(pastelR[2]) + ",G2=" + String(pastelG[2]) + ",B2=" + String(pastelB[2])
         + ",R3=" + String(pastelR[3]) + ",G3=" + String(pastelG[3]) + ",B3=" + String(pastelB[3])
         + ",NMODE=" + String(nuitMode)
         + ",NMON="  + String(nuitManOn ? 1 : 0)
         + ",NHON="  + String(nuitHeureOn)  + ":" + (nuitMinOn<10?"0":"")  + String(nuitMinOn)
         + ",NHOFF=" + String(nuitHeureOff) + ":" + (nuitMinOff<10?"0":"") + String(nuitMinOff)
         + ",HEURE=" + heureCourante;
  }
  return "ERR:UNKNOWN:" + cmd;
}

// ─── Routes HTTP ──────────────────────────────────────────
void handleRoot() {
  derniereRequete = millis();
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleManifest() {
  derniereRequete = millis();
  server.send_P(200, "application/manifest+json", MANIFEST_JSON);
}

void serveIconB64(const char* b64data) {
  derniereRequete = millis();
  // Décoder base64 en binaire
  String b64 = String(b64data);
  int len = b64.length();
  int outLen = (len * 3) / 4;
  uint8_t* buf = (uint8_t*)malloc(outLen);
  if (!buf) { server.send(500, "text/plain", "OOM"); return; }

  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int out = 0;
  for (int i = 0; i < len; i += 4) {
    auto idx = [&](char c) -> uint8_t {
      if (c == '=') return 0;
      const char* p = strchr(tbl, c);
      return p ? (p - tbl) : 0;
    };
    uint8_t a=idx(b64[i]), b=idx(b64[i+1]),
            cc=i+2<len?idx(b64[i+2]):0, d=i+3<len?idx(b64[i+3]):0;
    if (out < outLen) buf[out++] = (a<<2)|(b>>4);
    if (out < outLen && i+2<len && b64[i+2]!='=') buf[out++] = (b<<4)|(cc>>2);
    if (out < outLen && i+3<len && b64[i+3]!='=') buf[out++] = (cc<<6)|d;
  }
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "image/png", (const char*)buf, out);
  free(buf);
}

void handleIcon192() { serveIconB64(ICON_192); }
void handleIcon512() { serveIconB64(ICON_512); }

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

  chargerPrefs();  // restaure les réglages nuit sauvegardés

  server.on("/",             handleRoot);
  server.on("/manifest.json",handleManifest);
  server.on("/icon192.png",  handleIcon192);
  server.on("/icon512.png",  handleIcon512);
  server.on("/cmd",          handleCmd);
  server.onNotFound(handleNotFound);

  Serial.println("VIGILED pret — double-clic BRIGHT pour activer le WiFi");
}

// ─── LOOP ─────────────────────────────────────────────────
void loop() {
  gererWifi();

  gererBoutonMode();
  gererBoutonBright();
  gererFlashLimite();
  if (flashActif) return;

  // Mode nuit : éteindre si non autorisé
  if (!ledsAutorisees()) {
    strip.clear(); strip.show(); delay(50); return;
  }

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
