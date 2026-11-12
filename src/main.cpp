#include <Arduino.h>

// Experiment 01: minimal ESP32-C3 bring-up firmware.
// No e-paper, no Wi-Fi, no other peripherals.

static uint32_t heartbeat = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);  // let the native USB CDC port enumerate on the host

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" Smart Calendar - Experiment 01");
  Serial.println(" ESP32-C3 serial heartbeat test");
  Serial.println("==========================================");
  Serial.printf("Chip       : %s (revision %u)\n", ESP.getChipModel(),
                ESP.getChipRevision());
  Serial.printf("Cores      : %u\n", ESP.getChipCores());
  Serial.printf("CPU        : %u MHz\n", getCpuFrequencyMhz());
  Serial.printf("Flash      : %u MB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("SDK        : %s\n", ESP.getSdkVersion());
  Serial.printf("Reset cause: %d\n", static_cast<int>(esp_reset_reason()));
  Serial.println("==========================================");
}

void loop() {
  ++heartbeat;
  const uint32_t seconds = millis() / 1000;

  Serial.printf("[%lu s] heartbeat #%lu - Smart Calendar alive\n",
                static_cast<unsigned long>(seconds),
                static_cast<unsigned long>(heartbeat));

  delay(1000);
}
