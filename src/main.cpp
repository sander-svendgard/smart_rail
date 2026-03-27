#include <Arduino.h>
#include <WiFi.h>
#include "wificonnection.h"
#include "gyro.h"

const char* mqtt_server = "10.22.130.110";

WiFiConnection wifi("NTNU-IOT", "");
ICM20948Gyro gyro;

// Publiser gyrodata hvert 200ms
const unsigned long PUBLISH_INTERVAL = 200;
unsigned long lastPublish = 0;

void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println("=== Gyro testprogram ===");

  // Koble til WiFi og MQTT
  wifi.connect();
  delay(1000);
  wifi.setDestination(mqtt_server, 1883);
  wifi.startWebServer();

  Serial.print("IP: http://");
  Serial.println(WiFi.localIP());

  // Initialiser gyroskopet (standard I2C-pinner på ESP32: SDA=21, SCL=22)
  // Bruker ±250 dps – høyest presisjon, passer for rolige togbevegelser
  if (!gyro.init(GYRO_FS_250DPS)) {
    Serial.println("FEIL: Kunne ikke initialisere gyro! Sjekk kabling.");
    while (true) { delay(1000); }
  }

  Serial.println("--- KLAR ---");
}

void loop() {
  wifi.handleClient();

  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();

    // Les gyroskopdata (grader/sekund per akse)
    float gx, gy, gz;
    gyro.readGyro(gx, gy, gz);

    // Publiser hver akse til eget MQTT-topic
    wifi.publishMQTT("togbane/gyro/x", String(gx, 2));
    wifi.publishMQTT("togbane/gyro/y", String(gy, 2));
    wifi.publishMQTT("togbane/gyro/z", String(gz, 2));

    Serial.print("Gyro  X: "); Serial.print(gx, 2);
    Serial.print("  Y: ");     Serial.print(gy, 2);
    Serial.print("  Z: ");     Serial.print(gz, 2);
    Serial.println(" deg/s");
  }
}
