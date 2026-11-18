#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPmDNS.h>
extern "C" {
#include "hal_gpio.h"
}

// ---- Verified MSS110 / RTL8710CM pin map ----
static const int PIN_RELAY = 20;   // PA20, HIGH = relay ON
static const int PIN_LED_R = 13;   // PA13, common-anode: LOW = red on
static const int PIN_LED_G = 14;   // PA14, common-anode: LOW = green on
static const int BTN       = 19;   // LibreTiny D19 == PA23, active-low
static const char* AP_SSID   = "MSS110-RECOVERY";
static const char* MDNS_HOST = "mss110";
static const uint16_t HOLD_MS = 10000;   // button hold for recovery

// Optional: auto-toggle the relay every N milliseconds (0 = disabled).
// e.g. 900000 = 15 minutes, 30000 = 30 seconds. Timed from boot, not wall-clock.
#define RELAY_AUTO_TOGGLE_MS 0

// ---- Raw Realtek HAL GPIO (force-mux pads LibreTiny leaves on SDIO/SWD) ----
// NOTE: must NOT be called from setup()/early boot - it faults. Init in loop().
static hal_gpio_adapter_t g_adapter[32];
static bool g_inited[32] = {false};
static void rawDrive(int pin, int level){
  if(pin<0||pin>31) return;
  if(!g_inited[pin]){ hal_gpio_init(&g_adapter[pin], (uint32_t)pin); g_inited[pin]=true; }
  hal_gpio_set_dir(&g_adapter[pin], GPIO_OUT);
  hal_gpio_write(&g_adapter[pin], level?1:0);
}
static void ledRed(bool on){ rawDrive(PIN_LED_R, on?0:1); }
static void ledGreen(bool on){ rawDrive(PIN_LED_G, on?0:1); }

static bool relayState = false;   // defaults OFF at boot (no persistence)
static bool recovery   = false;
static WebServer server(80);
static void applyRelay(){ rawDrive(PIN_RELAY, relayState?1:0); }

static String page(){
  String h = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>MSS110 Recovery</title><style>body{font-family:sans-serif;max-width:520px;margin:1.2em auto;padding:0 12px}"
    "button{padding:9px 14px;margin:4px;font-size:16px}</style></head><body>"
    "<h2>MSS110 &mdash; Recovery</h2><p style=color:#888>mss110.local</p>");
  h += "<p>Relay: <b>"; h += (relayState?"ON":"OFF"); h += F("</b></p>");
  h += F("<p><button onclick=\"fetch('/relay?on=1').then(()=>location.reload())\">Relay ON</button>"
         "<button onclick=\"fetch('/relay?on=0').then(()=>location.reload())\">Relay OFF</button></p>"
         "<p><button onclick=\"if(confirm('Reboot device?'))fetch('/reboot')\">Reboot</button></p>"
         "<fieldset><legend>Update firmware (.uf2)</legend>"
         "<form method='POST' action='/update' enctype='multipart/form-data'>"
         "<input type='file' name='fw'><input type='submit' value='Flash'></form></fieldset>"
         "<p style='color:#888'>Power-cycle to leave recovery.</p></body></html>");
  return h;
}

static void enterRecovery(){
  if(recovery) return;
  recovery = true;
  ledRed(false); ledGreen(true);          // solid green = in recovery
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);                    // open network
  delay(300);
  IPAddress ip = WiFi.softAPIP();
  server.on("/", HTTP_GET, [](){ server.send(200,"text/html",page()); });
  server.on("/relay", HTTP_GET, [](){
    relayState=(server.arg("on").toInt()!=0); applyRelay();
    server.send(200,"text/plain",relayState?"ON":"OFF");
  });
  server.on("/reboot", HTTP_GET, [](){ server.send(200,"text/html","<h3>Rebooting...</h3>"); delay(300); LT.restart(); });
  server.on("/update", HTTP_POST,
    [](){ bool ok=!Update.hasError(); server.send(200,"text/html", ok?"<h3>OK - rebooting</h3>":"<h3>FAILED</h3>"); delay(500); if(ok) LT.restart(); },
    [](){ HTTPUpload& up=server.upload();
      if(up.status==UPLOAD_FILE_START){ if(!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); }
      else if(up.status==UPLOAD_FILE_WRITE){ Update.write(up.buf,up.currentSize); }
      else if(up.status==UPLOAD_FILE_END){ Update.end(true); } });
  server.begin();
  if(MDNS.begin(MDNS_HOST)) MDNS.addService("http","tcp",80);
  Serial.printf("[recovery] AP '%s'  http://mss110.local/  (http://%d.%d.%d.%d/)\r\n",
                AP_SSID, ip[0],ip[1],ip[2],ip[3]);
}

void setup(){
  Serial.begin(115200);
  delay(200);
  Serial.println("\r\n=== MSS110 firmware ===");
  pinMode(BTN, INPUT_PULLUP);
  // raw-HAL GPIO is initialized from loop() - touching it here faults.
}

void loop(){
  static bool hwInit=false;
  if(!hwInit){
    hwInit=true;
    delay(300);
    ledRed(false); ledGreen(false);
    relayState=false; applyRelay();
    // Boot-time recovery escape hatch: button held during power-up -> recovery.
    // Debounced so a floating/disconnected button can't trip it.
    int low=0;
    for(int i=0;i<40;i++){ if(digitalRead(BTN)==LOW) low++; delay(15); }
    if(low>=38){ Serial.println("[boot] button held -> recovery"); enterRecovery(); }
    else Serial.println("ready: short press=toggle relay, hold 10s=recovery");
  }

  if(recovery){ server.handleClient(); ledGreen(true); ledRed(false); delay(2); return; }

#if RELAY_AUTO_TOGGLE_MS > 0
  // Optional self-running relay toggle (interval from boot)
  static uint32_t lastAuto=0;
  if(millis()-lastAuto >= (uint32_t)RELAY_AUTO_TOGGLE_MS){
    lastAuto=millis();
    relayState=!relayState; applyRelay();
    Serial.printf("[auto] relay %s\r\n", relayState?"ON":"OFF");
  }
#endif

  static bool down=false, hold=false;
  static uint32_t t0=0;
  bool d = (digitalRead(BTN)==LOW);
  uint32_t now=millis();
  if(d && !down){ down=true; hold=false; t0=now; ledRed(true); }                    // press -> red
  if(d && down && !hold && now-t0>=HOLD_MS){ hold=true; Serial.println("[btn] 10s hold -> recovery"); enterRecovery(); }
  if(!d && down){                                                                   // release
    down=false;
    if(!hold){ relayState=!relayState; applyRelay(); Serial.printf("[btn] relay %s\r\n", relayState?"ON":"OFF"); }
    ledRed(false);
  }

  static String buf;
  while(Serial.available()){
    char c=Serial.read();
    if(c=='\n'||c=='\r'){ buf.trim(); if(buf=="reboot"){ delay(50); LT.restart(); } buf=""; }
    else buf+=c;
  }
  delay(5);
}
