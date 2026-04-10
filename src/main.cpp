#include <Arduino.h>
#include <WiFi.h>
#include "wificonnection.h"
#include "ultrasonic.h"
#include "gyro.h"

// ── Nettverksinnstillinger ────────────────────────────────────────────────
const char* MQTT_SERVER = "10.22.129.65";

// ── Deteksjonsparametere ──────────────────────────────────────────────────
const float         TRIGGER_DISTANCE  = 10.0f;  // cm — regnes som deteksjon
const float         MAX_DISTANCE      = 20.0f;  // cm — ignoreres over denne
const int           REQUIRED_READINGS = 5;      // Påfølgende målinger under terskel
const unsigned long ACTIVATION_DELAY  = 800;    // ms minimum mellom sensoraktiveringer
const unsigned long RESET_TIMEOUT     = 15000;  // ms uten aktivitet → reset

// ── Sensortilstander ──────────────────────────────────────────────────────
enum SensorState { WAITING, ACTIVE, DONE };
const char* STATE_STR[] = { "WAITING", "ACTIVE", "DONE" };

// ── Globale variabler (settes i setup basert på MAC) ─────────────────────
WiFiConnection wifi("NTNU-IOT", "");

Ultrasonic* sensors[3] = {nullptr, nullptr, nullptr};
int numSensors = 0;
int sensorOffset = 0;
int activeSensor = 0;
bool isFirstGroup = true;
bool isLastGroup = false;
bool waitingForStart = false;

unsigned long lastTriggerTime = 0;
unsigned long groupStartTime = 0;

// FIX 1: Høyere terskel for å hindre for rask aktivering
const unsigned long RESET_TIMEOUT = 15000;   // 15 sekunder
const unsigned long ACTIVATION_DELAY = 800;  // 800ms mellom sensorer
const float MAX_DISTANCE = 20.0;
const float TRIGGER_DISTANCE = 10.0;
const int REQUIRED_READINGS = 5;             // 5 x 100ms = 500ms sammenhengende deteksjon

int triggerCount = 0;

// FIX 2: groupActive hindrer reset-loop etter fullført runde
bool groupActive = false;

void publishAllSensorStatuses(int forActiveSensor, const char* activeStatus) {
  for (int i = 0; i < numSensors; i++) {
    int sensorNum = i + 1 + sensorOffset;
    String statusTopic = "togbane/sensor/" + String(sensorNum) + "/status";
    if (i == forActiveSensor) {
      wifi.publishMQTT(statusTopic.c_str(), activeStatus);
    } else if (i < forActiveSensor) {
      wifi.publishMQTT(statusTopic.c_str(), "DONE");
    } else {
      wifi.publishMQTT(statusTopic.c_str(), "WAITING");
    }
  }
}

void publishAllSleep() {
  for (int i = 0; i < numSensors; i++) {
    int sensorNum = i + 1 + sensorOffset;
    String statusTopic = "togbane/sensor/" + String(sensorNum) + "/status";
    wifi.publishMQTT(statusTopic.c_str(), "SLEEP");
  }
}

// FIX 2: Én felles reset-funksjon — setter alltid timestamps og groupActive riktig
void doReset(bool sendMqtt) {
  activeSensor = 0;
  triggerCount = 0;
  groupActive = false;        // Stopper reset-loopen
  groupStartTime = millis();  // Oppdater alltid
  lastTriggerTime = millis(); // Oppdater alltid

  if (!isFirstGroup) {
    waitingForStart = true;
    publishAllSleep();
  }

  if (sendMqtt) {
    wifi.publishMQTT("togbane/status", "RESET");
    Serial.println("RESET sendt.");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  String topicStr = String(topic);
  Serial.println("MQTT mottatt: " + topicStr + " = " + msg);

  if (!isFirstGroup && waitingForStart) {
    bool shouldStart =
      (topicStr == "togbane/gruppe1/ferdig" && sensorOffset == 2 && msg == "TOG_PASSERT") ||
      (topicStr == "togbane/gruppe2/ferdig" && sensorOffset == 4 && msg == "TOG_PASSERT");

    if (shouldStart) {
      Serial.println("Signal mottatt! Starter sensor " + String(1 + sensorOffset));
      waitingForStart = false;
      groupActive = true;
      activeSensor = 0;
      triggerCount = 0;
      lastTriggerTime = millis();
      groupStartTime = millis();
    }
  }
}

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
    isFirstGroup = true;
    isLastGroup = false;
    waitingForStart = false;
    groupActive = true;   // ESP32 #1 starter alltid aktiv
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
    groupActive = false;
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
    groupActive = false;
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
  wifi.setCallback(mqttCallback);
  wifi.startWebServer();

  if (!isFirstGroup) {
    if (sensorOffset == 2) {
      wifi.subscribe("togbane/gruppe1/ferdig");
      Serial.println("Venter på signal fra ESP32 #1...");
    } else if (sensorOffset == 4) {
      wifi.subscribe("togbane/gruppe2/ferdig");
      Serial.println("Venter på signal fra ESP32 #2...");
    }
    publishAllSleep();
  }

  Serial.print("IP: http://");
  Serial.println(WiFi.localIP());

  Serial.println("--- KALIBRERING ---");
  for (int i = 0; i < numSensors; i++) {
    float d = sensors[i]->measureDistance();
    Serial.println("Sensor " + String(i + 1 + sensorOffset) + " baseline: " + String(d) + " cm");
  }
  Serial.println("--- KLAR ---");

  groupStartTime = millis();
  lastTriggerTime = millis();  // FIX 2: Sett ved oppstart så timeout ikke trigger umiddelbart
}

void loop() {
  wifi.handleClient();

  // --- Venter på MQTT-signal ---
  if (waitingForStart) {
    static unsigned long lastSleepPublish = 0;
    if (millis() - lastSleepPublish > 5000) {
      lastSleepPublish = millis();
      publishAllSleep();
    }
    delay(100);
    return;
  }

  // FIX 2: Ikke gjør noe hvis gruppen ikke er aktiv (hindrer reset-loop)
  if (!groupActive) {
    delay(100);
    return;
  }

  // --- Timeout-sjekk (kun basert på lastTriggerTime) ---
  if (millis() - lastTriggerTime > RESET_TIMEOUT) {
    Serial.println("Timeout! Ingen aktivitet. Resetter.");
    doReset(true);

    // FIX 2: ESP32 #1 starter alltid neste runde selv
    if (isFirstGroup) {
      groupActive = true;
    }
    return;
  }

  if (activeSensor >= numSensors) {
    delay(100);
    return;
  }

  // --- Les aktiv sensor ---
  float distance = sensors[activeSensor]->measureDistance();
  int sensorNum = activeSensor + 1 + sensorOffset;

  if (distance > MAX_DISTANCE || distance <= 0) {
    distance = MAX_DISTANCE;
    triggerCount = 0;
  }

  String distTopic = "togbane/sensor/" + String(sensorNum) + "/distanse";
  wifi.publishMQTT(distTopic.c_str(), String(distance, 1));
  publishAllSensorStatuses(activeSensor, "WAITING");

  Serial.println("Sensor " + String(sensorNum) + ": " + String(distance) +
                 " cm (treff: " + String(triggerCount) + "/" + String(REQUIRED_READINGS) + ")");

  bool delayPassed = (millis() - lastTriggerTime > ACTIVATION_DELAY);

  if (distance < TRIGGER_DISTANCE && distance > 0.5 && delayPassed) {
    triggerCount++;

    if (triggerCount >= REQUIRED_READINGS) {
      String statusTopic = "togbane/sensor/" + String(sensorNum) + "/status";
      wifi.publishMQTT(statusTopic.c_str(), "ACTIVE");
      Serial.println(">>> Sensor " + String(sensorNum) + " AKTIVERT! <<<");

      lastTriggerTime = millis();
      activeSensor++;
      triggerCount = 0;

      if (activeSensor >= numSensors) {
        Serial.println("Alle sensorer på denne ESP32 aktivert!");

        for (int i = 0; i < numSensors; i++) {
          int sNum = i + 1 + sensorOffset;
          String sTopic = "togbane/sensor/" + String(sNum) + "/status";
          wifi.publishMQTT(sTopic.c_str(), "DONE");
        }

        if (isFirstGroup) {
          wifi.publishMQTT("togbane/gruppe1/ferdig", "TOG_PASSERT");
          wifi.publishMQTT("togbane/status", "GRUPPE1_FERDIG");
        } else if (!isLastGroup) {
          wifi.publishMQTT("togbane/gruppe2/ferdig", "TOG_PASSERT");
          wifi.publishMQTT("togbane/status", "GRUPPE2_FERDIG");
        } else {
          wifi.publishMQTT("togbane/status", "TOG_PASSERT");
        }

        delay(2000);

        // FIX 2: doReset() setter groupActive=false og timestamps riktig
        doReset(false);

        // FIX 2: ESP32 #1 reaktiverer seg selv for neste runde
        if (isFirstGroup) {
          groupActive = true;
          Serial.println("ESP32 #1: Klar for neste runde!");
        }

        Serial.println("Resetter til sensor " + String(1 + sensorOffset));
      } else {
        Serial.println("Venter på sensor " + String(activeSensor + 1 + sensorOffset) + "...");
      }
    }
  } else if (distance >= TRIGGER_DISTANCE || distance <= 0.5) {
    triggerCount = 0;
  }

  delay(100);
}