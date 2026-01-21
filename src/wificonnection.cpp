#include "wificonnection.h"

WiFiConnection::WiFiConnection(const char* ssid, const char* password)
    : ssid(ssid), password(password), server(80), lastData("No data yet") {}

void WiFiConnection::connect() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void WiFiConnection::startWebServer() {
  server.on("/", [this]() { handleRoot(); });
  server.on("/data", [this]() { handleData(); });
  server.begin();
  Serial.println("Web server started on port 80");
}

void WiFiConnection::handleClient() {
  server.handleClient();
}

void WiFiConnection::handleRoot() {
  String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
      <title>ESP32 Output</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        body { font-family: Arial; margin: 20px; background-color: #f0f0f0; }
        h1 { color: #333; }
        #data { background-color: white; padding: 15px; border-radius: 5px; white-space: pre-wrap; font-family: monospace; }
      </style>
      <script>
        function updateData() {
          fetch('/data')
            .then(r => r.text())
            .then(data => document.getElementById('data').innerText = data);
        }
        setInterval(updateData, 1000);
        updateData();
      </script>
    </head>
    <body>
      <h1>ESP32 Output Monitor</h1>
      <div id="data">Loading...</div>
    </body>
    </html>
  )";
  server.send(200, "text/html", html);
}

void WiFiConnection::handleData() {
  server.send(200, "text/plain", lastData);
}

void WiFiConnection::sendData(String data) {
  lastData = data;
  Serial.println(data);
}

String WiFiConnection::getLocalIP() {
  return WiFi.localIP().toString();
}

bool WiFiConnection::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void WiFiConnection::setDestination(const char* mqtt_server, int mqtt_port) {
  mqttClient.setClient(espClient);
  mqttClient.setServer(mqtt_server, mqtt_port);
  reconnectMQTT();
}

void WiFiConnection::reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Kobler til MQTT...");
    if (mqttClient.connect("ESP32Client")) {
      Serial.println("tilkoblet!");
    } else {
      Serial.print("feilet, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" prøver igjen om 5 sekunder");
      delay(5000);
    }
  }
}

void WiFiConnection::publishMQTT(const char* topic, String message) {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.publish(topic, message.c_str());
}
