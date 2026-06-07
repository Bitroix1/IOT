#include <Adafruit_NeoPixel.h>

#define PIN 12
#define NUMPIXELS 4
#define DELAYVAL 500

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  pixels.begin();
}

void loop() {
  pixels.clear();

  // Step 0: all LEDs off
  pixels.show();
  delay(DELAYVAL);

  for (int i = 0; i < NUMPIXELS; i++) {

    // Red -> Green gradient
    int red   = 255 - (255 * i) / (NUMPIXELS - 1);
    int green = (255 * i) / (NUMPIXELS - 1);

    pixels.setPixelColor(i, pixels.Color(red, green, 0));

    pixels.show();
    delay(DELAYVAL);
  }

  delay(1000);

  // Optional: turn everything off before repeating
  pixels.clear();
  pixels.show();
  delay(DELAYVAL);
}