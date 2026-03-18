#include "wificonnection.h"

WiFiConnection::WiFiConnection(const char* ssid, const char* password) 
  : ssid(ssid), password(password), server(80), mqttClient(espClient) {}

void WiFiConnection::connect() {
  WiFi.begin(ssid, password);
  Serial.print("Kobler til WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi tilkoblet!");
}

void WiFiConnection::startWebServer() {
  server.on("/", [this]() { handleRoot(); });
  server.on("/data", [this]() { handleData(); });
  server.begin();
  Serial.println("Webserver startet!");
}

void WiFiConnection::handleClient() {
  server.handleClient();
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
}

void WiFiConnection::sendData(String data) {
  lastData = data;
}

void WiFiConnection::setDestination(const char* mqtt_server, int mqtt_port) {
  mqttClient.setServer(mqtt_server, mqtt_port);
  reconnectMQTT();
}

void WiFiConnection::setCallback(MQTT_CALLBACK_SIGNATURE) {
  mqttClient.setCallback(callback);
}

void WiFiConnection::subscribe(const char* topic) {
  subscribedTopic = topic;
  if (mqttClient.connected()) {
    mqttClient.subscribe(topic);
    Serial.println("Abonnerer på: " + String(topic));
  }
}

void WiFiConnection::publishMQTT(const char* topic, String message) {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.publish(topic, message.c_str());
}

void WiFiConnection::reconnectMQTT() {
  if (mqttClient.connected()) return;

  // Prøv kun hvert 5. sekund, ikke blokker
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  Serial.print("Kobler til MQTT...");
  String clientId = "ESP32_" + WiFi.macAddress();
  clientId.replace(":", "");

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("tilkoblet som " + clientId);
    if (subscribedTopic != nullptr) {
      mqttClient.subscribe(subscribedTopic);
      Serial.println("Re-abonnert på: " + String(subscribedTopic));
    }
  } else {
    Serial.print("feilet, rc=");
    Serial.println(mqttClient.state());
  }
}

void WiFiConnection::handleRoot() {
  server.send(200, "text/html", "<h1>ESP32 Sensor</h1><p><a href='/data'>Se data</a></p>");
}

void WiFiConnection::handleData() {
  server.send(200, "text/plain", lastData);
}

String WiFiConnection::getLocalIP() {
  return WiFi.localIP().toString();
}

bool WiFiConnection::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}