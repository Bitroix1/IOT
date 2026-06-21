/*
 * ===========================================================================
 *  BUZZER SYSTEM — BUZZER NODE   (painlessMesh version)
 * ===========================================================================
 *  Flash onto EACH buzzer. Give every board a unique BUZZER_ID below.
 *
 *  On "READY" it vibrates + buzzes (your original START_ALERT behavior),
 *  pulsing every 500 ms until the button is pressed or "CLEAR" arrives.
 *  It announces itself to the master automatically (HELLO every 5 s), so the
 *  website learns it exists with no manual setup, and reports its button.
 *
 *  Wiring (your build):
 *    Motor   -> GPIO 21    (active-HIGH: HIGH = ON)
 *    Buzzer  -> GPIO 22    (active-LOW : LOW  = ON, HIGH = OFF)
 *    Button  -> GPIO 26    (other leg to GND, uses internal pull-up)
 * ===========================================================================
 */

#include "painlessMesh.h"

#define MESH_PREFIX     "MyESP32Mesh"
#define MESH_PASSWORD   "MeshPassword123"
#define MESH_PORT       5555

// ===========================================================================
#define BUZZER_ID  1          // <<<<<< CHANGE PER BOARD: 1, 2, 3, ...
// ===========================================================================

const int motorPin  = 21;
const int buzzerPin = 22;     // active-LOW
const int buttonPin = 26;

painlessMesh mesh;
Scheduler    userScheduler;

// ---- non-blocking alert pattern -------------------------------------------
//  togglesLeft = -1 -> run forever (until button / CLEAR)
//  togglesLeft >  0 -> finite number of flips, then stop (for short signals)
bool          alertActive = false;
bool          phaseOn     = false;
int           togglesLeft = -1;
unsigned long interval    = 500;
unsigned long lastToggle  = 0;

void setPeripherals(bool on) {
  digitalWrite(motorPin,  on ? HIGH : LOW);    // motor active-HIGH
  digitalWrite(buzzerPin, on ? LOW  : HIGH);   // buzzer active-LOW
}

void startPattern(unsigned long ivl, int toggles) {
  interval = ivl; togglesLeft = toggles;
  phaseOn = true; lastToggle = millis();
  alertActive = true;
  setPeripherals(true);
}

void stopAlert() {
  alertActive = false;
  setPeripherals(false);
}

void serviceAlert() {
  if (!alertActive) return;
  if (millis() - lastToggle < interval) return;
  lastToggle += interval;
  phaseOn = !phaseOn;
  setPeripherals(phaseOn);
  if (togglesLeft > 0 && --togglesLeft == 0) stopAlert();
}

// ---- talk back to the master (broadcast; only the master acts on these) ----
void ackCmd(const char *cmd) {
  mesh.sendBroadcast("ACK:" + String(BUZZER_ID) + ":" + cmd);
}

// ---- commands coming from the master --------------------------------------
void onReceive(uint32_t from, String &msg) {
  if      (msg == "READY")    { startPattern(500, -1); ackCmd("READY"); }   // full alert
  else if (msg == "QUESTION") { startPattern(120,  6); ackCmd("QUESTION"); } // quick burst
  else if (msg == "ASSIGN")   { startPattern(150,  1); ackCmd("ASSIGN"); }   // short confirm
  else if (msg == "CLEAR")    { stopAlert();           ackCmd("CLEAR"); }    // silence
  else if (msg == "PING")     { ackCmd("PING"); }                            // just reply
}

unsigned long lastHello = 0;
int           lastBtn   = HIGH;
unsigned long lastBtnT  = 0;

void setup() {
  Serial.begin(115200);
  pinMode(motorPin,  OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  setPeripherals(false);                 // clean start (buzzer OFF = HIGH)

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&onReceive);
}

void loop() {
  mesh.update();
  serviceAlert();

  unsigned long t = millis();

  // announce ourselves so the master maps id -> node (and to stay "online")
  if (t - lastHello > 5000) {
    mesh.sendBroadcast("HELLO:" + String(BUZZER_ID));
    lastHello = t;
  }

  // button: cancel locally and tell the master (e.g. customer collected order)
  int b = digitalRead(buttonPin);
  if (b != lastBtn && (t - lastBtnT) > 40) {
    lastBtnT = t;
    if (b == LOW) {
      if (alertActive) stopAlert();
      mesh.sendBroadcast("BUTTON:" + String(BUZZER_ID));
    }
    lastBtn = b;
  }
}
