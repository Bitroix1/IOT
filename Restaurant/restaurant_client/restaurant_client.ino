#include "painlessMesh.h"

#define MESH_PREFIX     "MyESP32Mesh"
#define MESH_PASSWORD   "MeshPassword123"
#define MESH_PORT       5555

painlessMesh mesh;
Scheduler userScheduler;

// Hardware Pins
const int motorPin = 21;
const int buzzerPin = 22;
const int buttonPin = 26;

// Alert States
bool isAlertActive = false;
bool alertToggleState = false;
unsigned long lastToggleTime = 0;
const unsigned long toggleInterval = 500; // Blink/buzz frequency (500ms)

// Turn off all physical peripherals
void turnOffAlerts() {
  digitalWrite(motorPin, LOW);    // Motor OFF
  digitalWrite(buzzerPin, HIGH);  // Buzzer OFF (Active-LOW)
}

void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Received message from %u: %s\n", from, msg.c_str());
  
  if (msg == "START_ALERT") {
    isAlertActive = true;
    lastToggleTime = millis();
    alertToggleState = true;
    Serial.println("-> Alert triggered by Master!");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(motorPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP); // Active-low button configuration

  // Enforce clean starting states
  turnOffAlerts();

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
}

void loop() {
  mesh.update(); // CRITICAL: Must run constantly without delay interference

  // Check if the cancel button is pressed (Grounded when clicked)
  if (digitalRead(buttonPin) == LOW) {
    if (isAlertActive) {
      isAlertActive = false;
      turnOffAlerts();
      Serial.println("Button pressed. Alert deactivated.");
    }
  }

  // Non-blocking alert routine handling
  if (isAlertActive) {
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastToggleTime >= toggleInterval) {
      lastToggleTime = currentMillis;
      alertToggleState = !alertToggleState; // Swap toggle tracking bit

      if (alertToggleState) {
        digitalWrite(motorPin, HIGH);   // Motor ON
        digitalWrite(buzzerPin, LOW);   // Buzzer ON (Active-LOW)
      } else {
        digitalWrite(motorPin, LOW);    // Motor OFF
        digitalWrite(buzzerPin, HIGH);  // Buzzer OFF (Active-LOW)
      }
    }
  }
}