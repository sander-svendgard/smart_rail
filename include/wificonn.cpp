#include <WiFi.h>
#include <WebServer.h>
#include "wificonn.h"

// Replace with your network credentials
const char* ssid = "NTNU-IOT";
const char* password = "";

WebServer server(80); // Web server on port 80

const int ledPin = 4; // Onboard LED pin (GPIO2 for ESP32)
bool ledState = false; // Track LED state

// HTML content for the web page
String HTMLPage() {
  String html = "<!DOCTYPE html><html>";
  html += "<head><title>ESP32 LED Control</title></head>";
  html += "<body style='text-align: center; font-family: Arial;'>";

  html += "<h1>ESP32 LED Control</h1>";
  html += ledState ? "<p>LED is <strong>ON</strong></p>" : "<p>LED is <strong>OFF</strong></p>";
  html += "<a href='/on' style='padding: 10px 20px; background-color: green; color: white; text-decoration: none;'>Turn ON</a>&nbsp;";
  html += "<a href='/off' style='padding: 10px 20px; background-color: red; color: white; text-decoration: none;'>Turn OFF</a>";

  html += "</body></html>";
  return html;
}

// Handle the root path "/"
void handleRoot() {
  server.send(200, "text/html", HTMLPage());
}

// Handle LED ON request
void handleLEDOn() {
  ledState = true;
  digitalWrite(ledPin, HIGH);
  server.send(200, "text/html", HTMLPage());
}

// Handle LED OFF request
void handleLEDOff() {
  ledState = false;
  digitalWrite(ledPin, LOW);
  server.send(200, "text/html", HTMLPage());
}
