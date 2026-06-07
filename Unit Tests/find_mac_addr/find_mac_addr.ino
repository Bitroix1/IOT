#include <WiFi.h>
#include <esp_now.h>

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("STARTED");
}

void loop() {
  Serial.println("LOOP");
  delay(1000);
}