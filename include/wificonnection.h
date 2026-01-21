#ifndef WIFICONNECTION_H
#define WIFICONNECTION_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>

class WiFiConnection {
public:
  WiFiConnection(const char* ssid, const char* password);
  
  void connect();
  void startWebServer();
  void handleClient();
  void sendData(String data);
  
  void setDestination(const char* mqtt_server, int mqtt_port);
  void publishMQTT(const char* topic, String message);
  
  String getLocalIP();
  bool isConnected();

private:
  const char* ssid;
  const char* password;
  WebServer server;
  String lastData;
  
  WiFiClient espClient;
  PubSubClient mqttClient;
  
  void handleRoot();
  void handleData();
  void reconnectMQTT();
};

#endif
