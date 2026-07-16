#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <EInkDisplay.h>

#include "catalog.h"
#include "config.h"
#include "display_cycle.h"
#include "wifi_manager.h"

namespace inky_bird_frame {
namespace {

EInkDisplay display(DISPLAY_SCLK, DISPLAY_MOSI, DISPLAY_CS, DISPLAY_DC, DISPLAY_RST,
                    DISPLAY_BUSY);
CatalogClient catalogClient(String(CONTROLLER_URL));
DisplayCycleRunner displayCycle(display);

void logResult(const DisplayCycleResult& result) {
  Serial.printf("display_update=%s taxon_id=%lu common_name=%s selection_reason=%s sha256=%s\n",
                result.updated ? "sent" : "unchanged", static_cast<unsigned long>(result.taxonId),
                result.commonName.c_str(), result.selectionReason.c_str(), result.sha256.c_str());
}

void sleepUntilNextCycle() {
  const uint64_t sleepMicros = static_cast<uint64_t>(ROTATION_MINUTES) * 60ULL * 1000000ULL;
  if (USE_DEEP_SLEEP) {
    Serial.printf("Sleeping for %lu minute(s)\n", static_cast<unsigned long>(ROTATION_MINUTES));
    display.deepSleep();
    esp_sleep_enable_timer_wakeup(sleepMicros);
    esp_deep_sleep_start();
  }
  delay(static_cast<unsigned long>(ROTATION_MINUTES) * 60UL * 1000UL);
}

}  // namespace
}  // namespace inky_bird_frame

void setup() {
  using namespace inky_bird_frame;

  Serial.begin(115200);
  delay(250);
  Serial.println("Inky Bird Frame firmware display node starting");

  connectToWifi();
  display.begin();
}

void loop() {
  using namespace inky_bird_frame;

  ensureWifiConnected();

  std::vector<CatalogEntry> entries;
  String errorMessage;
  if (!catalogClient.fetch(entries, errorMessage)) {
    Serial.printf("catalog_error=%s\n", errorMessage.c_str());
    sleepUntilNextCycle();
    return;
  }

  DisplayCycleResult result;
  if (!displayCycle.run(entries, false, result, errorMessage)) {
    Serial.printf("display_cycle_error=%s\n", errorMessage.c_str());
    sleepUntilNextCycle();
    return;
  }

  logResult(result);
  sleepUntilNextCycle();
}
