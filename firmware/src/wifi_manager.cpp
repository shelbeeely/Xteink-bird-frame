#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"

namespace inky_bird_frame {
namespace {

void waitForWifi() {
  while (WiFi.status() != WL_CONNECTED) {
    delay(WIFI_RETRY_DELAY_MS);
    Serial.print('.');
  }
  Serial.println();
  Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
}

}  // namespace

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to Wi-Fi SSID %s\n", WIFI_SSID);
  waitForWifi();
}

void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  Serial.println("Wi-Fi disconnected, reconnecting");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  waitForWifi();
}

}  // namespace inky_bird_frame
