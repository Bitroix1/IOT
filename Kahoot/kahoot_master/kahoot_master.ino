#include <map>
#include "painlessMesh.h"

#define MESH_PREFIX     "MyESP32Mesh"
#define MESH_PASSWORD   "MeshPassword123"
#define MESH_PORT       5555

painlessMesh mesh;
Scheduler userScheduler;

// Question Structural Blueprint
struct Question {
  String question;
  String answers[4];
  int correctAnswerIndex;
};

// Bank of 10 Quiz Questions
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

// Persistent Global Scoreboard Tracking
std::map<uint32_t, int> playerTotalScores;
std::map<uint32_t, bool> answeredThisRound;

// Shuffling sequence generator enforcing clean random cycles without repetitions
void shuffleQuestions() {
  for (int i = 9; i > 0; i--) {
    int j = random(0, i + 1);
    int temp = questionOrder[i];
    questionOrder[i] = questionOrder[j];
    questionOrder[j] = temp;
  }
  currentQuestionIndex = 0;
}

void sendNextQuestion();
Task taskNextQuestion(10000, TASK_FOREVER, &sendNextQuestion);

void sendNextQuestion() {
  // 1. Debugger Log: Print Leaderboards at the conclusion of each active round
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

  // Recycle and reshuffle upon reaching the limit of 10 items
  if (currentQuestionIndex >= 10) {
    Serial.println("All questions completed! Shuffling bank for a new match...");
    shuffleQuestions();
  }

  // Wipe temporary round buffers
  answeredThisRound.clear();

  int targetIdx = questionOrder[currentQuestionIndex];
  Question q = quizBank[targetIdx];

  // Build pipe-delimited packet payload
  String payload = "Q|" + q.question + "|" + q.answers[0] + "|" + q.answers[1] + "|" + q.answers[2] + "|" + q.answers[3] + "|" + String(q.correctAnswerIndex);
  
  mesh.sendBroadcast(payload);
  questionStartTime = millis();

  Serial.printf("Broadcasted Question %d/10: %s\n", currentQuestionIndex + 1, q.question.c_str());
}

// Inbound payload processing hook
void receivedCallback(uint32_t from, String &msg) {
  if (msg.startsWith("A|")) {
    // Prevent players from submitting multiple times in the same round
    if (answeredThisRound[from]) return; 
    answeredThisRound[from] = true;

    unsigned long responseTime = millis() - questionStartTime;
    int clientSelection = msg.substring(2).toInt();

    int activeRealIdx = questionOrder[currentQuestionIndex];
    int correctSelection = quizBank[activeRealIdx].correctAnswerIndex;

    if (clientSelection == correctSelection) {
      if (responseTime > 10000) responseTime = 10000;
      
      // Scaled Formula: Instant (0s) = 1000 pts | 5s = 500 pts | 10s = 0 pts
      int roundScore = (10000 - responseTime) / 10;
      if (roundScore < 0) roundScore = 0;

      playerTotalScores[from] += roundScore;
      Serial.printf("Node %u: CORRECT (Time: %lums, Earned: %d pts)\n", from, responseTime, roundScore);
    } else {
      Serial.printf("Node %u: INCORRECT (Selected option %d)\n", from, clientSelection + 1);
    }
  }
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0)); // Seed random generation from noise pin
  
  shuffleQuestions();

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  
  mesh.onReceive(&receivedCallback);

  // Bind the 10-second game clock to the network scheduler
  userScheduler.addTask(taskNextQuestion);
  taskNextQuestion.enable();
}

void loop() {
  mesh.update();
}