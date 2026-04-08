#include <Arduino.h>
#include <WiFi.h>
#include "wificonnection.h"
#include "ultrasonic.h"
#include "gyro.h"

// ── Nettverksinnstillinger ────────────────────────────────────────────────
const char* MQTT_SERVER = "10.22.128.115";

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
Ultrasonic* sensors[2] = { nullptr, nullptr };
ICM20948Gyro gyro;
bool gyroAvailable = false;

int         sensorOffset = 0;
const char* listenTopic  = nullptr;
const char* doneTopic    = nullptr;

SensorState  sensorState[2]      = { WAITING, WAITING };
int          consecutiveCount[2] = { 0, 0 };
int          activeSensor        = -1;

bool          pendingActivation = false;
int           pendingIdx        = -1;
unsigned long pendingTime       = 0;

unsigned long lastActivityTime = 0;
unsigned long lastGyroPublish  = 0;
const unsigned long GYRO_INTERVAL = 200;

// ── Hjelpefunksjoner ──────────────────────────────────────────────────────

void publishSensor(int localIdx, float dist, SensorState state) {
    int globalNum = sensorOffset + localIdx + 1;
    String topicDist   = "togbane/sensor/" + String(globalNum) + "/distanse";
    String topicStatus = "togbane/sensor/" + String(globalNum) + "/status";
    wifi.publishMQTT(topicDist.c_str(),   String(dist, 1));
    wifi.publishMQTT(topicStatus.c_str(), STATE_STR[state]);
}

void activateSensor(int localIdx) {
    sensorState[localIdx]      = ACTIVE;
    consecutiveCount[localIdx] = 0;
    activeSensor               = localIdx;
    lastActivityTime           = millis();
    Serial.println("Sensor " + String(sensorOffset + localIdx + 1) + " aktivert");
    publishSensor(localIdx, 0.0f, ACTIVE);
}

void resetAll() {
    Serial.println("RESET — tilbakestiller alle sensorer");
    activeSensor      = -1;
    pendingActivation = false;
    for (int i = 0; i < 2; i++) {
        sensorState[i]      = WAITING;
        consecutiveCount[i] = 0;
        publishSensor(i, 0.0f, WAITING);
    }
    wifi.publishMQTT("togbane/status", "RESET");
    lastActivityTime = millis();

    // Board #1 starter alltid umiddelbart etter reset
    if (listenTopic == nullptr) {
        activateSensor(0);
    }
}

// MQTT-callback — mottar TOG_PASSERT fra forrige board
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();
    Serial.println("MQTT [" + String(topic) + "]: " + msg);

    if (listenTopic != nullptr
        && String(topic) == String(listenTopic)
        && msg == "TOG_PASSERT")
    {
        Serial.println("Forrige gruppe ferdig — aktiverer sensor A om " +
                       String(ACTIVATION_DELAY) + "ms");
        pendingActivation = true;
        pendingIdx        = 0;
        pendingTime       = millis() + ACTIVATION_DELAY;
    }
}

// ── setup() ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);

    String mac = WiFi.macAddress();
    Serial.println("MAC: " + mac);

    // Boardidentifikasjon via MAC — setter sensorpinner, offset og topics
    if (mac == "10:97:BD:D4:DE:B0" || mac == "84:1F:E8:39:7A:58") {
        Serial.println("Board #1 — sensorer 1-2");
        sensorOffset = 0;
        listenTopic  = nullptr;
        doneTopic    = "togbane/gruppe1/ferdig";
        sensors[0]   = new Ultrasonic(26, 25);
        sensors[1]   = new Ultrasonic(32, 33);
    }
    else if (mac == "4C:C3:82:CC:E0:EC") {
        Serial.println("Board #2 — sensorer 3-4");
        sensorOffset = 2;
        listenTopic  = "togbane/gruppe1/ferdig";
        doneTopic    = "togbane/gruppe2/ferdig";
        sensors[0]   = new Ultrasonic(14, 27);
        sensors[1]   = new Ultrasonic(26, 25);
    }
    else if (mac == "A4:F0:0F:67:19:EC") {
        Serial.println("Board #3 — sensorer 5-6");
        sensorOffset = 4;
        listenTopic  = "togbane/gruppe2/ferdig";
        doneTopic    = nullptr;
        sensors[0]   = new Ultrasonic(26, 25);
        sensors[1]   = new Ultrasonic(32, 33);
    }
    else {
        Serial.println("UKJENT ESP32! MAC: " + mac);
        while (true) { delay(1000); }
    }

    for (int i = 0; i < 2; i++) {
        sensors[i]->init();
    }

    // Koble til WiFi og MQTT
    wifi.connect();
    delay(500);
    wifi.setCallback(mqttCallback);
    wifi.setDestination(MQTT_SERVER, 1883);

    if (listenTopic != nullptr) {
        wifi.subscribe(listenTopic);
    }

    wifi.startWebServer();
    Serial.print("IP: http://");
    Serial.println(WiFi.localIP());

    // Init gyro — kun board #1 har ICM-20948 koblet til I2C
    if (sensorOffset == 0) {
        gyroAvailable = gyro.init(GYRO_FS_250DPS);
        if (!gyroAvailable) Serial.println("Gyro ikke funnet — sjekk kabling");
    }

    lastActivityTime = millis();

    // Board #1 starter med sensor 1 aktiv med en gang
    if (listenTopic == nullptr) {
        activateSensor(0);
    }

    Serial.println("--- KLAR ---");
}

// ── loop() ───────────────────────────────────────────────────────────────
void loop() {
    wifi.handleClient();

    unsigned long now = millis();

    // ── Gyrodata (kun board #1) ───────────────────────────────────────────
    if (gyroAvailable && (now - lastGyroPublish >= GYRO_INTERVAL)) {
        lastGyroPublish = now;
        float gx, gy, gz;
        gyro.readGyro(gx, gy, gz);
        wifi.publishMQTT("togbane/gyro/x", String(gx, 2));
        wifi.publishMQTT("togbane/gyro/y", String(gy, 2));
        wifi.publishMQTT("togbane/gyro/z", String(gz, 2));
    }

    // ── Utsatt sensoraktivering (etter ACTIVATION_DELAY) ─────────────────
    if (pendingActivation && now >= pendingTime) {
        pendingActivation = false;
        activateSensor(pendingIdx);
    }

    // ── Reset-timeout ─────────────────────────────────────────────────────
    if (activeSensor != -1 && (now - lastActivityTime >= RESET_TIMEOUT)) {
        resetAll();
        return;
    }

    // ── Ingen aktiv sensor ────────────────────────────────────────────────
    if (activeSensor == -1) return;

    // ── Mål avstand fra aktiv sensor ─────────────────────────────────────
    float dist = sensors[activeSensor]->measureDistance();
    if (dist > MAX_DISTANCE) dist = MAX_DISTANCE;

    publishSensor(activeSensor, dist, ACTIVE);

    // ── Tell påfølgende treff under terskel ───────────────────────────────
    if (dist <= TRIGGER_DISTANCE) {
        consecutiveCount[activeSensor]++;
        lastActivityTime = now;
    } else {
        consecutiveCount[activeSensor] = 0;
    }

    // ── Sensor bekreftet — toget detektert ───────────────────────────────
    if (consecutiveCount[activeSensor] >= REQUIRED_READINGS) {
        int localIdx = activeSensor;
        activeSensor = -1;

        sensorState[localIdx] = DONE;
        publishSensor(localIdx, dist, DONE);
        Serial.println("Sensor " + String(sensorOffset + localIdx + 1) + ": DONE");

        int nextIdx = localIdx + 1;

        if (nextIdx < 2 && sensorState[nextIdx] == WAITING) {
            // Aktiver neste sensor på dette boardet etter forsinkelse
            pendingActivation = true;
            pendingIdx        = nextIdx;
            pendingTime       = now + ACTIVATION_DELAY;
        } else {
            // Begge sensorer på dette boardet er ferdige
            if (doneTopic != nullptr) {
                wifi.publishMQTT(doneTopic, "TOG_PASSERT");
                Serial.println("Publiserer TOG_PASSERT -> " + String(doneTopic));

                if (sensorOffset == 0)
                    wifi.publishMQTT("togbane/status", "GRUPPE1_FERDIG");
                else if (sensorOffset == 2)
                    wifi.publishMQTT("togbane/status", "GRUPPE2_FERDIG");
            } else {
                // Board #3 — toget har fullfort hele banen
                wifi.publishMQTT("togbane/status", "TOG_PASSERT");
                Serial.println("Toget har fullfort hele banen!");
            }
        }
    }
}
