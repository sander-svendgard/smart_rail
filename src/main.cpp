#include <Arduino.h>
#include <WiFi.h>
#include "wificonnection.h"
#include "ultrasonic.h"

const char* mqtt_server = "10.22.130.167";  //Må alltid sjekke at vi har riktig IP-adresse

WiFiConnection wifi("NTNU-IOT", "");

Ultrasonic* sensors[2] = {nullptr, nullptr};
int numSensors = 0;
int sensorOffset = 0;

const unsigned long PUBLISH_INTERVAL = 200; // ms mellom hver publisering
unsigned long lastPublish = 0;

void setup() {
  Serial.begin(9600);
  delay(1000);

  String mac = WiFi.macAddress();
  Serial.println("============================");
  Serial.println("MAC: " + mac);
  Serial.println("============================");

  if (mac == "10:97:BD:D4:DE:B0") {
    Serial.println("ESP32 #1 - Sensor 1-2");
    sensorOffset = 0;
    numSensors = 2;
    sensors[0] = new Ultrasonic(26, 25);
    sensors[1] = new Ultrasonic(32, 33);
  }
  else if (mac == "4C:C3:82:CC:E0:EC") {
    Serial.println("ESP32 #2 - Sensor 3-4");
    sensorOffset = 2;
    numSensors = 2;
    sensors[0] = new Ultrasonic(14, 27);
    sensors[1] = new Ultrasonic(26, 25);
  }
  else if (mac == "A4:F0:0F:67:19:EC") {  // Oppdater til faktisk MAC hvis feil
    Serial.println("ESP32 #3 - Sensor 5-6");
    sensorOffset = 4;
    numSensors = 2;
    sensors[0] = new Ultrasonic(26, 25);
    sensors[1] = new Ultrasonic(32, 33);
  }
  else {
    Serial.println("UKJENT ESP32! MAC: " + mac);
    while (true) { delay(1000); }
  }

  for (int i = 0; i < numSensors; i++) {
    sensors[i]->init();
  }

  wifi.connect();
  delay(1000);
  wifi.setDestination(mqtt_server, 1883);
  wifi.startWebServer();

  Serial.print("IP: http://");
  Serial.println(WiFi.localIP());
  Serial.println("--- KLAR ---");
}

void loop() {
  wifi.handleClient();

  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = millis();

    for (int i = 0; i < numSensors; i++) {
      float distance = sensors[i]->measureDistance();

      // Begrens til gyldig område
      if (distance <= 0 || distance > 400) distance = 400.0;

      int sensorNum = i + 1 + sensorOffset;
      String topic = "togbane/sensor/" + String(sensorNum) + "/distanse";
      wifi.publishMQTT(topic.c_str(), String(distance, 1));

      Serial.println("Sensor " + String(sensorNum) + ": " + String(distance, 1) + " cm");
    }
  }
}
