#include <map>
#include "painlessMesh.h"

#define MESH_PREFIX     "MyESP32Mesh"
#define MESH_PASSWORD   "MeshPassword123"
#define MESH_PORT       5555

painlessMesh mesh;
Scheduler userScheduler;

// ---------------------------- Kahoot state ---------------------------------
struct Question {
  String question;
  String answers[4];
  int correctAnswerIndex;
};

Question quizBank[10] = {
  {"When was Israel\nfounded?", "1948", "2007", "1969", "1939", 0},
  {"Capital of France?", "London", "Berlin", "Paris", "Madrid", 2},
  {"What is 5 + 7?", "10", "11", "12", "13", 2},
  {"HTML Color #FF0000?", "Blue", "Green", "Red", "Yellow", 2},
  {"Largest Ocean?", "Atlantic", "Indian", "Arctic", "Pacific", 3},
  {"How many continents?", "5", "6", "7", "8", 2},
  {"Formula for Water?", "CO2", "H2O", "O2", "NaCl", 1},
  {"Speed of Light Symbol?", "c", "x", "v", "l", 0},
  {"Boiling Pt of Water?", "90 C", "100 C", "120 C", "80 C", 1},
  {"Gravity acceleration?", "9.8 m/s2", "5.5 m/s2", "12 m/s2", "3.1 m/s2", 0}
};

int questionOrder[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
int currentQuestionIndex = 0;
unsigned long questionStartTime = 0;
bool matchStarted = false;
std::map<uint32_t, int> playerTotalScores;
std::map<uint32_t, bool> answeredThisRound;

// --------------------------- Restaurant state -------------------------------
std::map<int, uint32_t> nodeOf;   // buzzerId -> mesh nodeId

struct SerialCommand {
  char cmd[16];
  int id;
};

QueueHandle_t serialCommandQueue = nullptr;
SemaphoreHandle_t stateMutex = nullptr;

String serialLine;
unsigned long nextQuestionAt = 0;
const unsigned long questionInterval = 10000;

void shuffleQuestions() {
  for (int i = 9; i > 0; i--) {
    int j = random(0, i + 1);
    int temp = questionOrder[i];
    questionOrder[i] = questionOrder[j];
    questionOrder[j] = temp;
  }
  currentQuestionIndex = 0;
}

void sendNextQuestion() {
  if (matchStarted) {
    Serial.println("\n============== SCOREBOARD ==============");
    if (playerTotalScores.empty()) {
      Serial.println("No active player responses recorded.");
    } else {
      for (auto const& [node, totalScore] : playerTotalScores) {
        Serial.printf("Node ID: %u | Total Score: %d pts\n", node, totalScore);
      }
    }
    Serial.println("========================================\n");
    currentQuestionIndex++;
  } else {
    matchStarted = true;
  }

  if (currentQuestionIndex >= 10) {
    Serial.println("All questions completed! Shuffling bank for a new match...");
    shuffleQuestions();
  }

  answeredThisRound.clear();

  int targetIdx = questionOrder[currentQuestionIndex];
  Question q = quizBank[targetIdx];
  String payload = "Q|" + q.question + "|" + q.answers[0] + "|" + q.answers[1] + "|" + q.answers[2] + "|" + q.answers[3] + "|" + String(q.correctAnswerIndex);

  mesh.sendBroadcast(payload);
  questionStartTime = millis();

  Serial.printf("Broadcasted Question %d/10: %s\n", currentQuestionIndex + 1, q.question.c_str());
}

void sendCmd(int id, const String &cmd) {
  auto it = nodeOf.find(id);
  if (it == nodeOf.end()) {
    Serial.printf("LOG,buzzer %d not online yet\n", id);
    return;
  }

  bool ok = mesh.sendSingle(it->second, cmd);
  Serial.printf("DELIVERY,%d,%s\n", id, ok ? "OK" : "ERR");
}

void handleSerialLine(String s) {
  s.trim();
  if (!s.length()) return;

  int comma = s.indexOf(',');
  String cmd = (comma < 0) ? s : s.substring(0, comma);
  int id = (comma < 0) ? -1 : s.substring(comma + 1).toInt();
  cmd.toUpperCase();

  if (cmd == "READY" || cmd == "ASSIGN" || cmd == "QUESTION" || cmd == "CLEAR" || cmd == "PING") {
    SerialCommand command;
    strncpy(command.cmd, cmd.c_str(), sizeof(command.cmd) - 1);
    command.cmd[sizeof(command.cmd) - 1] = '\0';
    command.id = id;
    if (xQueueSend(serialCommandQueue, &command, 0) != pdTRUE) {
      Serial.println("LOG,command queue full");
    }
  } else {
    Serial.printf("LOG,unknown command: %s\n", cmd.c_str());
  }
}

void processSerialQueue() {
  SerialCommand command;
  while (xQueueReceive(serialCommandQueue, &command, 0) == pdTRUE) {
    sendCmd(command.id, String(command.cmd));
  }
}

// Reverse lookup: mesh nodeId -> buzzerId (the monitor keys everything by buzzerId).
int buzzerIdOfNode(uint32_t node) {
  for (auto const& [bid, nid] : nodeOf) if (nid == node) return bid;
  return -1;
}

void receivedCallback(uint32_t from, String &msg) {
  if (msg.startsWith("HELLO:")) {
    int id = msg.substring(6).toInt();
    nodeOf[id] = from;
    Serial.printf("ONLINE,%d,%u\n", id, from);
    return;
  }

  if (msg.startsWith("BUTTON:")) {
    Serial.printf("BUTTON,%d\n", msg.substring(7).toInt());
    return;
  }

  if (msg.startsWith("ACK:")) {
    int c2 = msg.indexOf(':', 4);
    int id = (c2 < 0) ? msg.substring(4).toInt() : msg.substring(4, c2).toInt();
    String cmd = (c2 < 0) ? "" : msg.substring(c2 + 1);
    Serial.printf("ACK,%d,%s\n", id, cmd.c_str());
    return;
  }

  if (msg.startsWith("A|")) {
    if (answeredThisRound[from]) return;
    answeredThisRound[from] = true;

    unsigned long responseTime = millis() - questionStartTime;
    int clientSelection = msg.substring(2).toInt();

    int activeRealIdx = questionOrder[currentQuestionIndex];
    int correctSelection = quizBank[activeRealIdx].correctAnswerIndex;

    if (clientSelection == correctSelection) {
      if (responseTime > 10000) responseTime = 10000;

      int roundScore = (10000 - responseTime) / 10;
      if (roundScore < 0) roundScore = 0;

      playerTotalScores[from] += roundScore;
      Serial.printf("Node %u: CORRECT (Time: %lums, Earned: %d pts)\n", from, responseTime, roundScore);
    } else {
      Serial.printf("Node %u: INCORRECT (Selected option %d)\n", from, clientSelection + 1);
    }

    // Machine-readable score for the web monitor (keyed by buzzerId).
    int bid = buzzerIdOfNode(from);
    if (bid >= 0) Serial.printf("SCORE,%d,%d\n", bid, playerTotalScores[from]);
  }
}

void core0LogicTask(void *parameter) {
  (void) parameter;

  for (;;) {
    mesh.update();

    if (millis() >= nextQuestionAt) {
      sendNextQuestion();
      nextQuestionAt = millis() + questionInterval;
    }

    processSerialQueue();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void core1SerialTask(void *parameter) {
  (void) parameter;

  for (;;) {
    while (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n') {
        handleSerialLine(serialLine);
        serialLine = "";
      } else if (ch != '\r') {
        serialLine += ch;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  stateMutex = xSemaphoreCreateMutex();
  serialCommandQueue = xQueueCreate(12, sizeof(SerialCommand));

  shuffleQuestions();

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);

  nextQuestionAt = millis() + questionInterval;

  xTaskCreatePinnedToCore(core0LogicTask, "core0Logic", 8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(core1SerialTask, "core1Serial", 4096, nullptr, 1, nullptr, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
