#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

Adafruit_SH1106G display(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);

  if (!display.begin(0x3C, true)) {
    Serial.println("SH1106 not found");
    while (1);
  }

  display.clearDisplay();

  display.setTextSize(4);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 20);

  display.println("Hello! :)");
  display.display();
}

void loop() {
}