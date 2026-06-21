#include "painlessMesh.h"

#define MESH_PREFIX     "MyESP32Mesh"
#define MESH_PASSWORD   "MeshPassword123"
#define MESH_PORT       5555

painlessMesh mesh;
Scheduler userScheduler;

void checkMeshAndTrigger();
// Task checks every 1 second for connected clients to trigger once on startup
Task taskStartupTrigger(1000, TASK_FOREVER, &checkMeshAndTrigger);

void checkMeshAndTrigger() {
  std::list<uint32_t> nodeList = mesh.getNodeList();

  // Wait until at least one client has successfully joined the mesh
  if (nodeList.size() > 0) {
    // Pick a random index from the connected nodes list
    int randomIndex = random(0, nodeList.size());
    
    // Advance an iterator to our random position
    auto it = nodeList.begin();
    std::advance(it, randomIndex);
    uint32_t targetClient = *it;

    // Send the execution signal to that specific client
    mesh.sendSingle(targetClient, "START_ALERT");
    Serial.printf("--- Startup Target Found! Sent signal to Node: %u ---\n", targetClient);

    // Disable this task permanently so it never runs again during this boot session
    taskStartupTrigger.disable();
  } else {
    Serial.println("Waiting for clients to connect to the mesh...");
  }
}

void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Received from %u: %s\n", from, msg.c_str());
}

void setup() {
  Serial.begin(115200);
  
  // Initialize random seed using noise from an unused analog pin
  randomSeed(analogRead(0));

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);

  // Queue up our one-time startup target seeker
  userScheduler.addTask(taskStartupTrigger);
  taskStartupTrigger.enable();
}

void loop() {
  mesh.update();
}