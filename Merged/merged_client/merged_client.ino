#include <TFT_eSPI.h>
#include "painlessMesh.h"

// Mesh Credentials - Must match Master perfectly
#define MESH_PREFIX     "MyESP32Mesh"
#define MESH_PASSWORD   "MeshPassword123"
#define MESH_PORT       5555

// Shared buzzer/client identity for the restaurant mesh side
#define BUZZER_ID       1

const int motorPin  = 21;
const int buzzerPin = 22;   // active-LOW
const int buttonPin  = 26;

painlessMesh mesh;
Scheduler userScheduler;
TFT_eSPI tft = TFT_eSPI();

// Master's Node ID calculated from MAC EC:62:60:9C:08:84
uint32_t masterNodeId = 1620838532;

enum ScreenMode : uint8_t {
  SCREEN_LOADING,
  SCREEN_QUIZ,
  SCREEN_RESULT,
  SCREEN_ALERT
};

enum AlertPattern : uint8_t {
  ALERT_NONE,
  ALERT_READY,
  ALERT_QUESTION,
  ALERT_ASSIGN
};

struct AppState {
  ScreenMode screenMode;
  ScreenMode screenBeforeAlert;
  AlertPattern alertPattern;
  bool alertActive;
  bool alertPhaseOn;
  int alertTogglesLeft;
  unsigned long alertInterval;
  unsigned long alertLastToggle;
  unsigned long resultStartTime;
  unsigned long loadingFrameStart;
  uint8_t loadingFrame;
  bool quizAvailable;
  bool lastSubmissionCorrect;
  int selectedBox;
  int correctAnswerBox;
  uint32_t uiRevision;
  uint32_t lastHelloTime;
  String quizQuestion;
  String quizAnswers[4];
};

struct UiSnapshot {
  ScreenMode screenMode;
  AlertPattern alertPattern;
  bool alertActive;
  bool alertPhaseOn;
  int alertTogglesLeft;
  uint8_t loadingFrame;
  bool lastSubmissionCorrect;
  int selectedBox;
  int correctAnswerBox;
  String quizQuestion;
  String quizAnswers[4];
  uint32_t uiRevision;
};

AppState appState;
SemaphoreHandle_t stateMutex = nullptr;

bool lastReading = HIGH;
bool buttonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long pressStartTime = 0;
bool holdReported = false;
bool pressConsumedByAlert = false;
const unsigned long holdDelay = 1000;
const unsigned long loadingFrameDelay = 80;
const unsigned long resultDelay = 1000;

bool loadingBaseDrawn = false;
bool alertBaseDrawn = false;
ScreenMode lastRenderedMode = SCREEN_LOADING;
uint32_t lastRenderedRevision = 0;

void setPeripherals(bool on) {
  digitalWrite(motorPin,  on ? HIGH : LOW);
  digitalWrite(buzzerPin, on ? LOW  : HIGH);
}

void initAppState() {
  appState.screenMode = SCREEN_LOADING;
  appState.screenBeforeAlert = SCREEN_LOADING;
  appState.alertPattern = ALERT_NONE;
  appState.alertActive = false;
  appState.alertPhaseOn = false;
  appState.alertTogglesLeft = 0;
  appState.alertInterval = 500;
  appState.alertLastToggle = 0;
  appState.resultStartTime = 0;
  appState.loadingFrameStart = 0;
  appState.loadingFrame = 0;
  appState.quizAvailable = false;
  appState.lastSubmissionCorrect = false;
  appState.selectedBox = 0;
  appState.correctAnswerBox = 0;
  appState.uiRevision = 1;
  appState.lastHelloTime = 0;
  appState.quizQuestion = "Waiting for game...";
  appState.quizAnswers[0] = "-";
  appState.quizAnswers[1] = "-";
  appState.quizAnswers[2] = "-";
  appState.quizAnswers[3] = "-";
}

void bumpUiRevisionLocked() {
  appState.uiRevision++;
}

void setQuizContentLocked(const String &question, const String &answer1, const String &answer2, const String &answer3, const String &answer4, int correctAnswerIndex) {
  appState.quizQuestion = question;
  appState.quizAnswers[0] = answer1;
  appState.quizAnswers[1] = answer2;
  appState.quizAnswers[2] = answer3;
  appState.quizAnswers[3] = answer4;
  appState.correctAnswerBox = constrain(correctAnswerIndex, 0, 3);
  appState.selectedBox = 0;
  appState.quizAvailable = true;

  if (!appState.alertActive) {
    appState.screenMode = SCREEN_QUIZ;
  }

  bumpUiRevisionLocked();
}

void startLoadingStateLocked(unsigned long now) {
  appState.screenMode = SCREEN_LOADING;
  appState.loadingFrame = 0;
  appState.loadingFrameStart = now;
  loadingBaseDrawn = false;
  bumpUiRevisionLocked();
}

void startResultStateLocked(bool isCorrect, unsigned long now) {
  appState.screenMode = SCREEN_RESULT;
  appState.lastSubmissionCorrect = isCorrect;
  appState.resultStartTime = now;
  bumpUiRevisionLocked();
}

void startAlertStateLocked(AlertPattern pattern, unsigned long now) {
  if (!appState.alertActive) {
    appState.screenBeforeAlert = appState.screenMode;
  }

  appState.alertActive = true;
  appState.alertPattern = pattern;
  appState.screenMode = SCREEN_ALERT;
  appState.alertPhaseOn = true;
  appState.alertLastToggle = now;
  appState.alertInterval = (pattern == ALERT_READY) ? 500 : (pattern == ALERT_QUESTION ? 120 : 150);
  appState.alertTogglesLeft = (pattern == ALERT_READY) ? -1 : (pattern == ALERT_QUESTION ? 6 : 1);
  setPeripherals(true);
  alertBaseDrawn = false;
  bumpUiRevisionLocked();
}

void stopAlertStateLocked(unsigned long now) {
  if (!appState.alertActive) {
    return;
  }

  appState.alertActive = false;
  appState.alertPattern = ALERT_NONE;
  appState.alertTogglesLeft = 0;
  appState.alertPhaseOn = false;
  setPeripherals(false);
  appState.screenMode = appState.screenBeforeAlert;

  if (appState.screenMode == SCREEN_RESULT && (now - appState.resultStartTime) >= resultDelay) {
    appState.screenMode = SCREEN_LOADING;
    appState.loadingFrame = 0;
    appState.loadingFrameStart = now;
    loadingBaseDrawn = false;
  }

  alertBaseDrawn = false;
  bumpUiRevisionLocked();
}

void updateLoadingAnimationLocked(unsigned long now) {
  if (appState.screenMode != SCREEN_LOADING || appState.alertActive) {
    return;
  }

  if ((now - appState.loadingFrameStart) >= loadingFrameDelay) {
    appState.loadingFrameStart = now;
    appState.loadingFrame = (appState.loadingFrame + 1) % 8;
    bumpUiRevisionLocked();
  }
}

void updateResultTimeoutLocked(unsigned long now) {
  if (appState.screenMode == SCREEN_RESULT && !appState.alertActive && (now - appState.resultStartTime) >= resultDelay) {
    startLoadingStateLocked(now);
  }
}

void updateAlertPatternLocked(unsigned long now) {
  if (!appState.alertActive) {
    return;
  }

  if ((now - appState.alertLastToggle) < appState.alertInterval) {
    return;
  }

  appState.alertLastToggle = now;
  appState.alertPhaseOn = !appState.alertPhaseOn;
  setPeripherals(appState.alertPhaseOn);

  if (appState.alertTogglesLeft > 0) {
    appState.alertTogglesLeft--;
    if (appState.alertTogglesLeft == 0) {
      stopAlertStateLocked(now);
      return;
    }
  }

  bumpUiRevisionLocked();
}

String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
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

void drawQuestionText(const String &questionText, int16_t x, int16_t y) {
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

void drawLoadingBase() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  const int16_t centerX = tft.width() / 2;
  const int16_t centerY = tft.height() / 2 - 10;
  tft.drawString("Waiting for question...", centerX, centerY + 92);
}

void drawLoadingFrame(uint8_t frameIndex) {
  const uint16_t trailColor1 = tft.color565(180, 180, 180);
  const uint16_t trailColor2 = tft.color565(120, 120, 120);
  const uint16_t trailColor3 = tft.color565(70, 70, 70);

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
    int8_t age = (int8_t)((frameIndex + 8 - i) % 8);
    uint16_t color = trailColor3;

    if (age == 0) color = TFT_WHITE;
    else if (age == 1) color = trailColor1;
    else if (age == 2) color = trailColor2;
    else if (age == 3) color = trailColor3;
    else color = TFT_BLACK;

    tft.fillRect(ringX[i] - cellSize / 2, ringY[i] - cellSize / 2, cellSize, cellSize, color);
  }
}

void drawKahootScreen(const UiSnapshot &snap) {
  const int16_t screenWidth = tft.width();
  const int16_t screenHeight = tft.height();
  const int16_t halfWidth = screenWidth / 2;
  const int16_t questionBandHeight = screenHeight / 5;
  const int16_t answersTop = questionBandHeight;
  const int16_t answersHeight = screenHeight - questionBandHeight;
  const int16_t topHalfHeight = answersHeight / 2;
  const int16_t bottomHalfHeight = answersHeight - topHalfHeight;

  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, screenWidth, questionBandHeight, TFT_BLACK);
  drawQuestionText(snap.quizQuestion, 12, 8);

  drawAnswerTile(0, answersTop, halfWidth, topHalfHeight, tft.color565(255, 165, 0), snap.quizAnswers[0]);
  drawAnswerTile(halfWidth, answersTop, screenWidth - halfWidth, topHalfHeight, TFT_RED, snap.quizAnswers[1]);
  drawAnswerTile(0, answersTop + topHalfHeight, halfWidth, bottomHalfHeight, TFT_DARKGREEN, snap.quizAnswers[2]);
  drawAnswerTile(halfWidth, answersTop + topHalfHeight, screenWidth - halfWidth, bottomHalfHeight, TFT_BLUE, snap.quizAnswers[3]);

  switch (snap.selectedBox) {
    case 0: drawSelectionBox(0, answersTop, halfWidth, topHalfHeight); break;
    case 1: drawSelectionBox(halfWidth, answersTop, screenWidth - halfWidth, topHalfHeight); break;
    case 2: drawSelectionBox(0, answersTop + topHalfHeight, halfWidth, bottomHalfHeight); break;
    case 3: drawSelectionBox(halfWidth, answersTop + topHalfHeight, screenWidth - halfWidth, bottomHalfHeight); break;
  }
}

void drawResultScreen(const UiSnapshot &snap) {
  const uint16_t background = snap.lastSubmissionCorrect ? TFT_GREEN : TFT_RED;
  tft.fillScreen(background);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, background);

  const int16_t centerX = tft.width() / 2;
  const int16_t centerY = tft.height() / 2;
  const int16_t markSize = 28;
  const int16_t lineWidth = 6;

  if (snap.lastSubmissionCorrect) {
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

uint16_t alertBackground(AlertPattern pattern) {
  switch (pattern) {
    case ALERT_READY: return TFT_RED;
    case ALERT_QUESTION: return tft.color565(180, 110, 0);
    case ALERT_ASSIGN: return tft.color565(0, 90, 180);
    default: return TFT_BLACK;
  }
}

const char *alertLabel(AlertPattern pattern) {
  switch (pattern) {
    case ALERT_READY: return "READY";
    case ALERT_QUESTION: return "QUESTION";
    case ALERT_ASSIGN: return "ASSIGN";
    default: return "ALERT";
  }
}

void drawAlertScreen(const UiSnapshot &snap) {
  uint16_t background = alertBackground(snap.alertPattern);
  uint16_t dimColor = tft.color565(70, 70, 70);
  uint16_t brightColor = TFT_WHITE;

  tft.fillScreen(background);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, background);
  tft.setTextSize(2);

  const int16_t centerX = tft.width() / 2;
  const int16_t centerY = tft.height() / 2 + 2;
  tft.drawString(alertLabel(snap.alertPattern), centerX, centerY - 52);
  tft.drawString("BUZZER ACTIVE", centerX, centerY - 24);

  const int16_t cellSize = 14;
  const int16_t gap = 7;
  const int16_t step = cellSize + gap;
  const int16_t leftX = centerX - step;
  const int16_t midX = centerX;
  const int16_t rightX = centerX + step;
  const int16_t topY = centerY + 22 - step;
  const int16_t midY = centerY + 22;
  const int16_t bottomY = centerY + 22 + step;

  const int16_t ringX[8] = { leftX, midX, rightX, rightX, rightX, midX, leftX, leftX };
  const int16_t ringY[8] = { topY, topY, topY, midY, bottomY, bottomY, bottomY, midY };

  for (uint8_t i = 0; i < 8; i++) {
    uint16_t color = snap.alertPhaseOn ? brightColor : dimColor;
    tft.fillRect(ringX[i] - cellSize / 2, ringY[i] - cellSize / 2, cellSize, cellSize, color);
  }

  tft.drawString("Press to dismiss", centerX, centerY + 92);
}

void renderUiSnapshot(const UiSnapshot &snap) {
  if (snap.screenMode != lastRenderedMode) {
    loadingBaseDrawn = false;
    alertBaseDrawn = false;
  }

  switch (snap.screenMode) {
    case SCREEN_LOADING:
      if (!loadingBaseDrawn) {
        drawLoadingBase();
        loadingBaseDrawn = true;
      }
      drawLoadingFrame(snap.loadingFrame);
      break;
    case SCREEN_QUIZ:
      drawKahootScreen(snap);
      break;
    case SCREEN_RESULT:
      drawResultScreen(snap);
      break;
    case SCREEN_ALERT:
      drawAlertScreen(snap);
      break;
  }

  lastRenderedMode = snap.screenMode;
  lastRenderedRevision = snap.uiRevision;
}

void copySnapshot(UiSnapshot &snap) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    snap.screenMode = appState.screenMode;
    snap.alertPattern = appState.alertPattern;
    snap.alertActive = appState.alertActive;
    snap.alertPhaseOn = appState.alertPhaseOn;
    snap.alertTogglesLeft = appState.alertTogglesLeft;
    snap.loadingFrame = appState.loadingFrame;
    snap.lastSubmissionCorrect = appState.lastSubmissionCorrect;
    snap.selectedBox = appState.selectedBox;
    snap.correctAnswerBox = appState.correctAnswerBox;
    snap.quizQuestion = appState.quizQuestion;
    snap.quizAnswers[0] = appState.quizAnswers[0];
    snap.quizAnswers[1] = appState.quizAnswers[1];
    snap.quizAnswers[2] = appState.quizAnswers[2];
    snap.quizAnswers[3] = appState.quizAnswers[3];
    snap.uiRevision = appState.uiRevision;
    xSemaphoreGive(stateMutex);
  }
}

void requestQuizSelectionStep() {
  int selectedBox = 0;

  if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    if (appState.screenMode == SCREEN_QUIZ && !appState.alertActive) {
      appState.selectedBox = (appState.selectedBox + 1) % 4;
      bumpUiRevisionLocked();
    }
    selectedBox = appState.selectedBox;
    xSemaphoreGive(stateMutex);
  }

  Serial.printf("Option %d selected\n", selectedBox + 1);
}

void submitQuizAnswer(unsigned long now) {
  int selectedBox = 0;
  int correctAnswerBox = 0;
  bool canSubmit = false;

  if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    if (appState.screenMode == SCREEN_QUIZ && !appState.alertActive) {
      selectedBox = appState.selectedBox;
      correctAnswerBox = appState.correctAnswerBox;
      canSubmit = true;
      startResultStateLocked(selectedBox == correctAnswerBox, now);
    }
    xSemaphoreGive(stateMutex);
  }

  if (canSubmit) {
    Serial.printf("Option %d submitted to Master\n", selectedBox + 1);
    String answerMsg = "A|" + String(selectedBox);
    mesh.sendSingle(masterNodeId, answerMsg);
  }
}

void handleButtonLogic(unsigned long now) {
  int reading = digitalRead(buttonPin);
  if (reading != lastReading) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        pressStartTime = now;
        holdReported = false;
        pressConsumedByAlert = false;

        if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
          if (appState.alertActive) {
            pressConsumedByAlert = true;
            stopAlertStateLocked(now);
            mesh.sendBroadcast("BUTTON:" + String(BUZZER_ID));
          }
          xSemaphoreGive(stateMutex);
        }
      } else {
        if (!pressConsumedByAlert) {
          if (!holdReported && (now - pressStartTime) < holdDelay) {
            requestQuizSelectionStep();
          }
        }
        holdReported = false;
        pressConsumedByAlert = false;
      }
    }

    if (!pressConsumedByAlert && buttonState == LOW && !holdReported && (now - pressStartTime) >= holdDelay) {
      holdReported = true;
      submitQuizAnswer(now);
    }
  }

  lastReading = reading;
}

void handleBuzzerCommand(const String &msg, unsigned long now) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  if (msg == "READY") {
    startAlertStateLocked(ALERT_READY, now);
    mesh.sendBroadcast("ACK:" + String(BUZZER_ID) + ":READY");
  } else if (msg == "QUESTION") {
    startAlertStateLocked(ALERT_QUESTION, now);
    mesh.sendBroadcast("ACK:" + String(BUZZER_ID) + ":QUESTION");
  } else if (msg == "ASSIGN") {
    startAlertStateLocked(ALERT_ASSIGN, now);
    mesh.sendBroadcast("ACK:" + String(BUZZER_ID) + ":ASSIGN");
  } else if (msg == "CLEAR") {
    stopAlertStateLocked(now);
    mesh.sendBroadcast("ACK:" + String(BUZZER_ID) + ":CLEAR");
  } else if (msg == "PING") {
    mesh.sendBroadcast("ACK:" + String(BUZZER_ID) + ":PING");
  }

  xSemaphoreGive(stateMutex);
}

void receivedCallback(uint32_t from, String &msg) {
  unsigned long now = millis();

  if (msg.startsWith("Q|")) {
    masterNodeId = from;

    String q = getValue(msg, '|', 1);
    String a0 = getValue(msg, '|', 2);
    String a1 = getValue(msg, '|', 3);
    String a2 = getValue(msg, '|', 4);
    String a3 = getValue(msg, '|', 5);
    int correctIdx = getValue(msg, '|', 6).toInt();

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
      setQuizContentLocked(q, a0, a1, a2, a3, correctIdx);
      xSemaphoreGive(stateMutex);
    }

    return;
  }

  if (msg == "READY" || msg == "QUESTION" || msg == "ASSIGN" || msg == "CLEAR" || msg == "PING") {
    handleBuzzerCommand(msg, now);
    return;
  }

  if (msg.startsWith("HELLO:") || msg.startsWith("BUTTON:") || msg.startsWith("ACK:")) {
    return;
  }
}

void logicTask(void *parameter) {
  (void) parameter;

  for (;;) {
    unsigned long now = millis();

    mesh.update();

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
      updateAlertPatternLocked(now);
      updateResultTimeoutLocked(now);
      updateLoadingAnimationLocked(now);

      if ((now - appState.lastHelloTime) > 5000) {
        appState.lastHelloTime = now;
        mesh.sendBroadcast("HELLO:" + String(BUZZER_ID));
      }

      xSemaphoreGive(stateMutex);
    }

    handleButtonLogic(now);
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void serviceUiCore() {
  UiSnapshot snap;
  copySnapshot(snap);

  if (snap.uiRevision != lastRenderedRevision) {
    renderUiSnapshot(snap);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(motorPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  setPeripherals(false);

  tft.init();
  tft.setRotation(1);

  stateMutex = xSemaphoreCreateMutex();
  initAppState();

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);

  UiSnapshot snap;
  copySnapshot(snap);
  renderUiSnapshot(snap);

  xTaskCreatePinnedToCore(logicTask, "logicTask", 8192, nullptr, 2, nullptr, 0);
}

void loop() {
  serviceUiCore();
  delay(10);
}