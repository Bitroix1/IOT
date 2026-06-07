const int buttonPin = 23;

bool lastReading = HIGH;
bool buttonState = HIGH;

unsigned long lastDebounceTime = 0;

const unsigned long debounceDelay = 50;

void setup() {
  Serial.begin(115200);

  // Internal pull-up: pin is HIGH normally, LOW when pressed
  pinMode(buttonPin, INPUT_PULLUP);

  Serial.println("Ready. Press the button.");
}

void loop() {
  bool reading = digitalRead(buttonPin);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        Serial.println("Button pressed!");
      }
    }
  }

  lastReading = reading;
}