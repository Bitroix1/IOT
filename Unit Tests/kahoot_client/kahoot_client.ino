#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

const int buttonPin = 26;

bool lastReading = HIGH;
bool buttonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long pressStartTime = 0;
bool holdReported = false;
const unsigned long holdDelay = 1000;
bool showingLoadingScreen = false;
unsigned long loadingStartTime = 0;
const unsigned long loadingDelay = 2000;
unsigned long loadingFrameStart = 0;
uint8_t loadingFrame = 0;
const unsigned long loadingFrameDelay = 80;
bool showingResultScreen = false;
bool lastSubmissionCorrect = false;
unsigned long resultStartTime = 0;
const unsigned long resultDelay = 1000;

int selectedBox = 0;
int correctAnswerBox = 0;

//default question and answers for failsafe
String quizQuestion = "Which box is orange?";
String quizAnswers[4] = {
  "Orange",
  "Red",
  "Green",
  "Blue"
};

void setQuizContent(const String &question, const String &answer1, const String &answer2, const String &answer3, const String &answer4, int correctAnswerIndex) {
  quizQuestion = question;
  quizAnswers[0] = answer1;
  quizAnswers[1] = answer2;
  quizAnswers[2] = answer3;
  quizAnswers[3] = answer4;
  correctAnswerBox = constrain(correctAnswerIndex, 0, 3);
}

void drawSelectionBox(int16_t x, int16_t y, int16_t w, int16_t h) {
  const int16_t border = 8;

  for (int16_t offset = 0; offset < border; offset++) {
    tft.drawRect(x + offset, y + offset, w - offset * 2, h - offset * 2, TFT_WHITE);
  }
}

void drawAnswerTile(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fillColor, const String &answerText) {
  tft.fillRect(x, y, w, h, fillColor);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fillColor);
  tft.drawString(answerText, x + w / 2, y + h / 2);
}

void drawQuestionText(const String &questionText, int16_t x, int16_t y, int16_t width) {
  int newlineIndex = questionText.indexOf('\n');

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  if (newlineIndex >= 0) {
    String line1 = questionText.substring(0, newlineIndex);
    String line2 = questionText.substring(newlineIndex + 1);
    tft.drawString(line1, x, y);
    tft.drawString(line2, x, y + 18);
  } else {
    tft.drawString(questionText, x, y + 9);
  }
}

void drawKahootSymbol(int16_t cx, int16_t cy, int16_t size, uint16_t color, uint8_t symbol) {
  switch (symbol) {
    case 0:
      tft.fillTriangle(cx, cy - size, cx - size, cy + size, cx + size, cy + size, color);
      break;
    case 1:
      tft.fillTriangle(cx, cy - size, cx - size, cy, cx + size, cy, color);
      tft.fillTriangle(cx, cy + size, cx - size, cy, cx + size, cy, color);
      break;
    case 2:
      tft.fillCircle(cx, cy, size, color);
      break;
    case 3:
      tft.fillRect(cx - size, cy - size, size * 2, size * 2, color);
      break;
  }
}

void drawKahootScreen() {
  const int16_t screenWidth = tft.width();
  const int16_t screenHeight = tft.height();
  const int16_t halfWidth = screenWidth / 2;
  const int16_t halfHeight = screenHeight / 2;
  const int16_t questionBandHeight = screenHeight / 5;
  const int16_t answersTop = questionBandHeight;
  const int16_t answersHeight = screenHeight - questionBandHeight;
  const int16_t topHalfHeight = answersHeight / 2;
  const int16_t bottomHalfHeight = answersHeight - topHalfHeight;

  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, screenWidth, questionBandHeight, TFT_BLACK);
  drawQuestionText(quizQuestion, 12, 8, screenWidth - 24);

  drawAnswerTile(0, answersTop, halfWidth, topHalfHeight, tft.color565(255, 165, 0), quizAnswers[0]);
  drawAnswerTile(halfWidth, answersTop, screenWidth - halfWidth, topHalfHeight, TFT_RED, quizAnswers[1]);
  drawAnswerTile(0, answersTop + topHalfHeight, halfWidth, bottomHalfHeight, TFT_DARKGREEN, quizAnswers[2]);
  drawAnswerTile(halfWidth, answersTop + topHalfHeight, screenWidth - halfWidth, bottomHalfHeight, TFT_BLUE, quizAnswers[3]);

  switch (selectedBox) {
    case 0:
      drawSelectionBox(0, answersTop, halfWidth, topHalfHeight);
      break;
    case 1:
      drawSelectionBox(halfWidth, answersTop, screenWidth - halfWidth, topHalfHeight);
      break;
    case 2:
      drawSelectionBox(0, answersTop + topHalfHeight, halfWidth, bottomHalfHeight);
      break;
    case 3:
      drawSelectionBox(halfWidth, answersTop + topHalfHeight, screenWidth - halfWidth, bottomHalfHeight);
      break;
  }
}

void drawLoadingScreen() {
  const uint16_t background = TFT_BLACK;
  const uint16_t trailColor1 = tft.color565(180, 180, 180);
  const uint16_t trailColor2 = tft.color565(120, 120, 120);
  const uint16_t trailColor3 = tft.color565(70, 70, 70);

  tft.fillScreen(background);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, background);
  tft.setTextSize(2);

  const int16_t centerX = tft.width() / 2;
  const int16_t centerY = tft.height() / 2 - 10;
  const int16_t cellSize = 18;
  const int16_t gap = 8;
  const int16_t step = cellSize + gap;
  const int16_t leftX = centerX - step;
  const int16_t midX = centerX;
  const int16_t rightX = centerX + step;
  const int16_t topY = centerY - step;
  const int16_t midY = centerY;
  const int16_t bottomY = centerY + step;

  const int16_t ringX[8] = { leftX, midX, rightX, rightX, rightX, midX, leftX, leftX };
  const int16_t ringY[8] = { topY, topY, topY, midY, bottomY, bottomY, bottomY, midY };

  for (uint8_t i = 0; i < 8; i++) {
    int8_t age = (int8_t)((loadingFrame + 8 - i) % 8);
    uint16_t color = trailColor3;

    if (age == 0) {
      color = TFT_WHITE;
    } else if (age == 1) {
      color = trailColor1;
    } else if (age == 2) {
      color = trailColor2;
    } else if (age == 3) {
      color = trailColor3;
    }

    tft.fillRect(ringX[i] - cellSize / 2, ringY[i] - cellSize / 2, cellSize, cellSize, color);
  }

  tft.drawString("Loading...", centerX, centerY + 92);
}

void drawResultScreen(bool isCorrect) {
  const uint16_t background = isCorrect ? TFT_GREEN : TFT_RED;

  tft.fillScreen(background);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, background);

  const int16_t centerX = tft.width() / 2;
  const int16_t centerY = tft.height() / 2;
  const int16_t markSize = 28;
  const int16_t lineWidth = 6;

  if (isCorrect) {
    for (int16_t i = 0; i < lineWidth; i++) {
      tft.drawLine(centerX - 22, centerY + 6 + i, centerX - 2, centerY + 24 + i, TFT_WHITE);
      tft.drawLine(centerX - 2, centerY + 24 + i, centerX + 26, centerY - 14 + i, TFT_WHITE);
    }
  } else {
    for (int16_t i = 0; i < lineWidth; i++) {
      tft.drawLine(centerX - markSize, centerY - markSize + i, centerX + markSize, centerY + markSize + i, TFT_WHITE);
      tft.drawLine(centerX - markSize, centerY + markSize + i, centerX + markSize, centerY - markSize + i, TFT_WHITE);
    }
  }
}

void startLoadingScreen() {
  showingLoadingScreen = true;
  showingResultScreen = false;
  loadingStartTime = millis();
  loadingFrameStart = 0;
  loadingFrame = 0;
  drawLoadingScreen();
}

void startResultScreen(bool isCorrect) {
  showingResultScreen = true;
  showingLoadingScreen = false;
  lastSubmissionCorrect = isCorrect;
  resultStartTime = millis();
  drawResultScreen(isCorrect);
}

void setup() {
  tft.init();
  tft.setRotation(1);

  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  setQuizContent("When was Israel\nfounded?", "1948", "2007", "1969", "1939", 0);
  drawKahootScreen();
}

void loop() {
  if (showingResultScreen) {
    if (millis() - resultStartTime >= resultDelay) {
      showingResultScreen = false;
      startLoadingScreen();
    }
    return;
  }

  if (showingLoadingScreen) {
    if (millis() - loadingFrameStart >= loadingFrameDelay) {
      loadingFrameStart = millis();
      loadingFrame = (loadingFrame + 1) % 12;
      drawLoadingScreen();
    }

    if (millis() - loadingStartTime >= loadingDelay) {
      showingLoadingScreen = false;
      drawKahootScreen();
    }
    return;
  }

  bool reading = digitalRead(buttonPin);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        pressStartTime = millis();
        holdReported = false;
      } else {
        if (!holdReported && (millis() - pressStartTime) < holdDelay) {
          selectedBox = (selectedBox + 1) % 4;
          drawKahootScreen();

          Serial.print("Option ");
          Serial.print(selectedBox + 1);
          Serial.println(" selected");
        }

        holdReported = false;
      }
    }

    if (buttonState == LOW && !holdReported && (millis() - pressStartTime) >= holdDelay) {
      holdReported = true;

      Serial.print("Option ");
      Serial.print(selectedBox + 1);
      Serial.println(" submitted");

      startResultScreen(selectedBox == correctAnswerBox);
    }
  }

  lastReading = reading;
}