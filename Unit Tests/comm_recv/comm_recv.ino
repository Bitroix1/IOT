#include <NetworkClientSecure.h>
#include <WiFiClientSecure.h>
#include <ssl_client.h>

#include <WiFi.h>
#include <esp_now.h>

typedef struct {
  char msg[32];
} Message;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Message m;
  memcpy(&m, data, sizeof(m));

  Serial.print("Received: ");
  Serial.println(m.msg);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("STARTED");
  
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onReceive);

  Serial.println("Receiver ready");
}

void loop() {}