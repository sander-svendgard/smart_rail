#ifndef WIFICONNECTION_H
#define WIFICONNECTION_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

class WiFiConnection {
public:
  WiFiConnection(const char* ssid, const char* password);
  
  void connect();
  void startWebServer();
  void handleClient();
  void sendData(String data);
  
  String getLocalIP();
  bool isConnected();

private:
  const char* ssid;
  const char* password;
  WebServer server;
  String lastData;
  
  void handleRoot();
  void handleData();
};

#endif
