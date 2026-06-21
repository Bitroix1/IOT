/*
 * ===========================================================================
 *  BUZZER SYSTEM — GATEWAY / MASTER   (painlessMesh version)
 * ===========================================================================
 *  Flash onto the ONE ESP32 that stays plugged into your computer (USB).
 *  It bridges the web monitor and the mesh:
 *
 *     web monitor  <--USB serial-->  MASTER  <--painlessMesh-->  buzzers
 *
 *  The website sends plain text like "READY,3". We look up which mesh node
 *  is buzzer #3 and forward the command word ("READY") to just that node.
 *  Buzzers announce themselves (HELLO:<id>) so the id->node map builds itself.
 *
 *  Needs the same libraries your mesh code already uses: painlessMesh,
 *  TaskScheduler, ArduinoJson, AsyncTCP.
 *
 *  SERIAL PROTOCOL (newline-terminated)
 *    Website -> Master:        Master -> Website:
 *      ASSIGN,<id>               ONLINE,<id>,<node>   buzzer announced itself
 *      READY,<id>                ACK,<id>,<cmd>       buzzer confirmed a command
 *      QUESTION,<id>             BUTTON,<id>           buzzer button pressed
 *      CLEAR,<id>                DELIVERY,<id>,OK|ERR  mesh send result
 *      PING,<id>                 LOG,<text>           free-form debug line
 * ===========================================================================
 */

#include "painlessMesh.h"
#include <map>

#define MESH_PREFIX     "MyESP32Mesh"
#define MESH_PASSWORD   "MeshPassword123"
#define MESH_PORT       5555

painlessMesh mesh;
Scheduler    userScheduler;

std::map<int, uint32_t> nodeOf;     // buzzerId -> mesh nodeId

// ---- messages coming up from the mesh -------------------------------------
void onReceive(uint32_t from, String &msg) {
  int colon   = msg.indexOf(':');
  String tag  = (colon < 0) ? msg : msg.substring(0, colon);
  String rest = (colon < 0) ? ""  : msg.substring(colon + 1);

  if (tag == "HELLO") {
    int id = rest.toInt();
    nodeOf[id] = from;
    Serial.printf("ONLINE,%d,%u\n", id, from);
  } else if (tag == "BUTTON") {
    Serial.printf("BUTTON,%d\n", rest.toInt());
  } else if (tag == "ACK") {
    int c2  = rest.indexOf(':');
    int id  = (c2 < 0) ? rest.toInt() : rest.substring(0, c2).toInt();
    String cmd = (c2 < 0) ? "" : rest.substring(c2 + 1);
    Serial.printf("ACK,%d,%s\n", id, cmd.c_str());
  }
  // any other mesh chatter is ignored
}

// ---- send a command word down to a specific buzzer ------------------------
void sendCmd(int id, const String &cmd) {
  auto it = nodeOf.find(id);
  if (it == nodeOf.end()) {
    Serial.printf("LOG,buzzer %d not online yet\n", id);
    return;
  }
  bool ok = mesh.sendSingle(it->second, cmd);
  Serial.printf("DELIVERY,%d,%s\n", id, ok ? "OK" : "ERR");
}

// ---- parse one line of text from the website ------------------------------
String line;
void handleSerialLine(String s) {
  s.trim();
  if (!s.length()) return;
  int comma  = s.indexOf(',');
  String cmd = (comma < 0) ? s : s.substring(0, comma);
  int id     = (comma < 0) ? -1 : s.substring(comma + 1).toInt();
  cmd.toUpperCase();

  if (cmd == "READY" || cmd == "ASSIGN" || cmd == "QUESTION" ||
      cmd == "CLEAR" || cmd == "PING") {
    sendCmd(id, cmd);
  } else {
    Serial.printf("LOG,unknown command: %s\n", cmd.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  mesh.setDebugMsgTypes(ERROR);    // keep the serial line clean for the website
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&onReceive);
  Serial.println("LOG,gateway/master ready");
}

void loop() {
  mesh.update();                   // must run constantly
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n')      { handleSerialLine(line); line = ""; }
    else if (ch != '\r') { line += ch; }
  }
}
