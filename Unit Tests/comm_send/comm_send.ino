#include <WiFi.h>
#include <esp_now.h>

uint8_t receiverMAC[] = {0xB0,0xA7,0x32,0xDE,0x95,0xD8};

typedef struct {
  char msg[32];
} Message;

Message m;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("Sender ready");
}

void loop() {
  strcpy(m.msg, "Hello ESP B");

  esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&m, sizeof(m));

  if (result == ESP_OK) {
    Serial.println("Sent OK");
  } else {
    Serial.println("Send failed");
  }

  delay(1000);
}