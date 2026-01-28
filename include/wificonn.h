#pragma once
#include <WiFi.h>
#include <WebServer.h>

String HTMLPage();

void handleRoot();
void handleLEDOn();
void handleLEDOff();

