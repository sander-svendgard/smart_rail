#include <Arduino.h>
#include <WiFi.h>
#include "wificonnection.h"


const int trigPin = 33;
const int echoPin = 32;

#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701

WiFiConnection wifi("YOUR_SSID", "YOUR_PASSWORD");

long duration;
float distanceCm;
float distanceInch;

void setup() {
  Serial.begin(115200); 
  delay(1000);
  
  pinMode(trigPin, OUTPUT); 
  pinMode(echoPin, INPUT);
  
  wifi.connect();
  delay(1000);
  wifi.startWebServer();
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
  
  delay(500);
}
