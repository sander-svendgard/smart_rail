#include <Arduino.h>
#include <WiFi.h>
#include "wificonnection.h"
#include "ultrasonic.h"

const char* mqtt_server = "10.22.128.83";

WiFiConnection wifi("NTNU-IOT", "");

Ultrasonic* sensors[3] = {nullptr, nullptr, nullptr};
int numSensors = 0;  // Antall sensorer på denne ESP32
int sensorOffset = 0;
int activeSensor = 0;
bool isFirstGroup = true;
bool isLastGroup = false;
bool waitingForStart = false;

unsigned long lastTriggerTime = 0;
const unsigned long RESET_TIMEOUT = 10000;
const unsigned long ACTIVATION_DELAY = 2000;
const float MAX_DISTANCE = 20.0;
const float TRIGGER_DISTANCE = 10.0;
const int REQUIRED_READINGS = 3;

int triggerCount = 0;

// Callback når MQTT-melding mottas
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  String topicStr = String(topic);
  Serial.println("MQTT mottatt: " + topicStr + " = " + msg);

  if (!isFirstGroup && waitingForStart) {
    // ESP32 #2 lytter på gruppe1
    if (topicStr == "togbane/gruppe1/ferdig" && msg == "TOG_PASSERT") {
      Serial.println("Signal mottatt! Starter sensor " + String(1 + sensorOffset));
      waitingForStart = false;
      activeSensor = 0;
      triggerCount = 0;
      lastTriggerTime = millis();
    }
    // ESP32 #3 lytter på gruppe2
    if (topicStr == "togbane/gruppe2/ferdig" && msg == "TOG_PASSERT") {
      Serial.println("Signal mottatt! Starter sensor " + String(1 + sensorOffset));
      waitingForStart = false;
      activeSensor = 0;
      triggerCount = 0;
      lastTriggerTime = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);  // ← Endre fra 9600 til 115200
  delay(1000);

  // Print MAC FØR alt annet
  String mac = WiFi.macAddress();
  Serial.println("============================");
  Serial.println("MAC: " + mac);
  Serial.println("============================");

  if (mac == "10:97:BD:D4:DE:B0") {
    Serial.println("ESP32 #1 - Sensor 1-2");
    sensorOffset = 0;
    numSensors = 2;
    isFirstGroup = true;
    isLastGroup = false;
    waitingForStart = false;
    sensors[0] = new Ultrasonic(26, 25);
    sensors[1] = new Ultrasonic(32, 33);
  }
  else if (mac == "4C:C3:82:CC:E0:EC") {
    Serial.println("ESP32 #2 - Sensor 3-4");
    sensorOffset = 2;
    numSensors = 2;
    isFirstGroup = false;
    isLastGroup = false;
    waitingForStart = true;
    sensors[0] = new Ultrasonic(14, 27);
    sensors[1] = new Ultrasonic(26, 25);
  }
  else if (mac == "A4:F0:0F:67:19:EC") {
    Serial.println("ESP32 #3 - Sensor 5-6");
    sensorOffset = 4;
    numSensors = 2;
    isFirstGroup = false;
    isLastGroup = true;
    waitingForStart = true;
    sensors[0] = new Ultrasonic(26, 25);  // ← Sett riktige pins
    sensors[1] = new Ultrasonic(32, 33);  // ← Sett riktige pins
  }
  else {
    Serial.println("UKJENT ESP32! MAC: " + mac);
    Serial.println("Legg til denne MAC-adressen i koden.");
    while (true) { delay(1000); }
  }

  for (int i = 0; i < numSensors; i++) {
    sensors[i]->init();
  }

  wifi.connect();
  delay(1000);

  wifi.setDestination(mqtt_server, 1883);
  wifi.setCallback(mqttCallback);
  wifi.startWebServer();

  // Subscribe basert på hvilken gruppe
  if (!isFirstGroup) {
    if (sensorOffset == 2) {
      wifi.subscribe("togbane/gruppe1/ferdig");
      Serial.println("Venter på signal fra ESP32 #1...");
    } else if (sensorOffset == 4) {
      wifi.subscribe("togbane/gruppe2/ferdig");
      Serial.println("Venter på signal fra ESP32 #2...");
    }
  }

  Serial.print("IP: http://");
  Serial.println(WiFi.localIP());

  // Kalibrering
  Serial.println("--- KALIBRERING ---");
  for (int i = 0; i < numSensors; i++) {
    float d = sensors[i]->measureDistance();
    Serial.println("Sensor " + String(i + 1 + sensorOffset) + " baseline: " + String(d) + " cm");
  }
  Serial.println("--- KLAR ---");
}

void loop() {
  wifi.handleClient();

  // Hvis venter på start-signal
  if (waitingForStart) {
    for (int i = 0; i < numSensors; i++) {
      int sensorNum = i + 1 + sensorOffset;
      String statusTopic = "togbane/sensor/" + String(sensorNum) + "/status";
      wifi.publishMQTT(statusTopic.c_str(), "SLEEP");
    }
    delay(1000);
    return;
  }

  // Reset hvis ingen aktivitet
  if (activeSensor > 0 && (millis() - lastTriggerTime > RESET_TIMEOUT)) {
    Serial.println("Timeout! Resetter.");
    activeSensor = 0;
    triggerCount = 0;
    if (!isFirstGroup) waitingForStart = true;
    wifi.publishMQTT("togbane/status", "RESET");
  }

  // Les kun aktiv sensor
  if (activeSensor < numSensors) {
    float distance = sensors[activeSensor]->measureDistance();
    int sensorNum = activeSensor + 1 + sensorOffset;

    if (distance > MAX_DISTANCE || distance <= 0) {
      distance = MAX_DISTANCE;
      triggerCount = 0;
    }

    String distTopic = "togbane/sensor/" + String(sensorNum) + "/distanse";
    wifi.publishMQTT(distTopic.c_str(), String(distance, 1));

    String statusTopic = "togbane/sensor/" + String(sensorNum) + "/status";
    wifi.publishMQTT(statusTopic.c_str(), "WAITING");

    Serial.println("Sensor " + String(sensorNum) + ": " + String(distance) + " cm (treff: " + String(triggerCount) + "/" + String(REQUIRED_READINGS) + ")");

    bool delayPassed = (millis() - lastTriggerTime > ACTIVATION_DELAY) || activeSensor == 0;

    if (distance < TRIGGER_DISTANCE && distance > 0.5 && delayPassed) {
      triggerCount++;

      if (triggerCount >= REQUIRED_READINGS) {
        wifi.publishMQTT(statusTopic.c_str(), "ACTIVE");
        Serial.println(">>> Sensor " + String(sensorNum) + " AKTIVERT! <<<");

        lastTriggerTime = millis();
        activeSensor++;
        triggerCount = 0;

        if (activeSensor >= numSensors) {
          Serial.println("Alle sensorer på denne ESP32 aktivert!");

          // Send signal til neste gruppe
          if (isFirstGroup) {
            wifi.publishMQTT("togbane/gruppe1/ferdig", "TOG_PASSERT");
            wifi.publishMQTT("togbane/status", "GRUPPE1_FERDIG");
          } else if (!isLastGroup) {
            wifi.publishMQTT("togbane/gruppe2/ferdig", "TOG_PASSERT");
            wifi.publishMQTT("togbane/status", "GRUPPE2_FERDIG");
          } else {
            wifi.publishMQTT("togbane/status", "TOG_PASSERT");
          }

          delay(3000);
          activeSensor = 0;
          triggerCount = 0;

          if (!isFirstGroup) waitingForStart = true;

          Serial.println("Resetter til sensor " + String(1 + sensorOffset));
        } else {
          Serial.println("Venter på sensor " + String(activeSensor + 1 + sensorOffset) + "...");
        }
      }
    } else {
      triggerCount = 0;
    }
  }

  delay(300);
}
