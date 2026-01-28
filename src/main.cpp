#include <Arduino.h>
#include <WiFi.h>
#include "wificonnection.h"
#include "wificonn.h"

const char* mqtt_server = "10.22.57.147"; // Oliver sin ip adresse 

const int trigPin = 33;
const int echoPin = 32;

#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701

WiFiConnection wifi("NTNU-IOT", "");

long duration;
float distanceCm;
float distanceInch;
bool seen = true;
String incomingString;

void setup() {
  Serial.begin(9600); 
  delay(1000);

  pinMode(trigPin, OUTPUT); 
  pinMode(echoPin, INPUT);
  wifi.connect();
  delay(1000);
  
  wifi.setDestination(mqtt_server, 1883);
  wifi.startWebServer();

  Serial.println("\n ESP32 WEB server");
  Serial.print("Tilgang til web serveren på: http://");
  Serial.println(WiFi.localIP());
  Serial.println("--------\n");
}

void loop() {
  wifi.handleClient();
  
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  duration = pulseIn(echoPin, HIGH);
  
  distanceCm = duration * SOUND_SPEED/2;
  distanceInch = distanceCm * CM_TO_INCH;

  String output = "Distance (cm): " + String(distanceCm) + " | Distance (inch): " + String(distanceInch);
  wifi.sendData(output);
  
  // Send til MQTT server
  wifi.publishMQTT("sensor/distance", output);
  
  delay(500);
}
