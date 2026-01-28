#include "ultrasonic.h"

Ultrasonic::Ultrasonic(int trigpin, int echopin):
  trigpin(trigpin), echopin(echopin){}

void Ultrasonic::init(){
  pinMode(trigpin, OUTPUT);
  pinMode(echopin, INPUT);
}

float Ultrasonic::measureDistance(){
  digitalWrite(trigpin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigpin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigpin, LOW);

  long duration = pulseIn(echopin, HIGH);
  float distanceCm = duration * SOUND_SPEED / 2; 
  
  return distanceCm; 

}


